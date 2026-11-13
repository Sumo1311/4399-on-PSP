// SPDX-License-Identifier: GPL-3.0-or-later
import fs from'node:fs';import path from'node:path';import crypto from'node:crypto';import{methodAdapter}from'./source_method_adapter.mjs';
const[reference,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE OUTPUT.json');
const k='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/',s='upstream/CORE_Shared/src/net/play5d/game/bvn/';
const files={hurt:k+'fighter/ctrler/mc/FighterMcHurtCtrl.as',main:k+'fighter/FighterMain.as',base:k+'interfaces/BaseGameSprite.as',hit:k+'fighter/models/HitVO.as',config:k+'GameConfig.as',types:s+'data/HitType.as',state:s+'data/fighter/FighterActionState.as',pose:s+'data/fighter/FighterSpecialFrame.as',action:k+'fighter/FighterAction.as',logic:k+'ctrler/GameLogic.as',ctrl:k+'fighter/ctrler/FighterMcCtrler.as'};
const original=Object.fromEntries(Object.entries(files).map(([key,file])=>[key,fs.readFileSync(path.join(reference,file),'utf8')]));
// super.beHit is the empty BaseGameSprite method. Keep the invocation as an
// explicit declared helper instead of inventing a superclass runtime.
const source={...original,main:original.main.replace(/super\.beHit\(hitvo, hitRect\);/,'baseBeHit(hitvo, hitRect);')};
const constants=key=>Object.fromEntries([...original[key].matchAll(/public static (?:const|var)\s+(\w+)\s*:\s*(?:int|uint|Number|String)\s*=\s*([^;]+);/g)].filter(x=>/^[\d\s.*+\-]+$|^'[^']*'$|^"[^"]*"$/.test(x[2].trim())).map(x=>[x[1],Function('return ('+x[2]+')')()]));
const GameConfig=constants('config'),HitType=constants('types'),FighterActionState=constants('state'),FighterSpecialFrame=constants('pose');
class BaseGameSprite{}class FighterMain extends BaseGameSprite{}class Bullet{}class FighterAttacker extends BaseGameSprite{}
const GameMode={currentMode:0,TRAINING:1},GameLogic={},FighterVoice={HURT:1,HURT_FLY:2,DIE:3};class FighterEvent{constructor(type){this.type=type;}}Object.assign(FighterEvent,{DIE:1,HURT:2,DO_ACTION:3});let current;
const math={...Object.fromEntries(Object.getOwnPropertyNames(Math).map(k=>[k,Math[k]])),random(){return current.random();}};
const EffectCtrl={I:{doHitEffect(){current.effect(1);},doDefenseEffect(){current.effect(4);},doSteelHitEffect(){current.effect(6);},doEffectById(){current.effect(5);}}};
const FighterEventDispatcher={dispatchEvent(f,e){if(e===1){++current.dies;current.event(2);}if(e===2)current.event(3);}};
const method=methodAdapter(source,{GameConfig,HitType,FighterActionState,FighterSpecialFrame,BaseGameSprite,FighterMain,Bullet,FighterAttacker,GameMode,GameLogic,FighterVoice,FighterEvent,FighterEventDispatcher,EffectCtrl,Math:math,int:x=>Math.trunc(x)});
const methods=(key,names)=>Object.fromEntries(names.map(n=>[n,method(key,n)]));
HitType._isHeavyTypes=original.types.match(/_isHeavyTypes:[\s\S]*?\[([^\]]+)\]/)[1].split(',').map(x=>HitType[x.trim()]);HitType.isHeavy=method('types','isHeavy');GameLogic.checkFighterDie=method('logic','checkFighterDie');
const hurtMethods=methods('hurt',['beHit','checkDefDirect','doHurt','doSteelHurt','doDefenseHit','doBreakDefense','doHurtAnimate','renderBeHitGap']);
const mainMethods=methods('main',['beHit','useEnergy','hasEnergy','addQi']),hitMethods=methods('hit',['isWeakHit','checkEnemyFighter','isCatch']);
const clearState=method('action','clearState'),clearAction=method('action','clearAction'),loseHp=method('base','loseHp'),doAction=method('ctrl','doAction');
let seed=0x3b5d27a1;const random=()=>{seed^=seed<<13;seed^=seed>>>17;seed^=seed<<5;return(seed>>>0)/4294967296;};
const pick=a=>a[Math.floor(random()*a.length)],bit=()=>random()<.5;const rows=[],coverage={};
for(const hz of[30,60])for(let scenario=0;scenario<4096;++scenario){
 GameConfig.FPS_GAME=hz;const hp=pick([0,.1,1,5,11,50,100,1000]),alive=bit(),energy=pick([0,1,17.2,37,100,150]),maxEnergy=pick([37,99.5,100]),overload=bit(),guard=bit(),steel=bit(),superSteel=bit(),allow=bit(),defRate=pick([.1,1,1.5,2,3]),defense=pick([0,0,3,25]),air=bit(),side=bit(),ownerKind=pick([0,1,2,3]),ownerMinion=bit(),minion=bit(),ownerReverse=bit(),facing=pick([-1,1]),ownerX=pick([340,398,400,406,450,520]),training=bit(),hurtAction=bit()&&scenario%8===0;
 const action={clearState,clearAction,hurtAction:hurtAction?'counter':null};action.clearState();
 const f=Object.assign(new FighterMain(),mainMethods,{hp,hpMax:1000,isAlive:alive,energy,energyMax:maxEnergy,energyOverLoad:overload,isAllowLoseHP:allow,defenseRate:defRate,defense,isInAir:air,isApplyG:true,isSteelBody:steel,isSuperSteelBody:superSteel,isAllowBeHit:true,direct:facing,x:400,actionState:hurtAction?16:0,qi:0,qiMax:300,_energyAddGap:0,_currentHurts:null,
  musouEnemyData:minion?{isBoss:false}:null,_vec:{x:0,y:0},baseBeHit(){},dispatchEvent(){++current.actions;current.event(9);},getIsTouchSide(){return side;},getCtrler(){return {getVoiceCtrl(){return{playVoice(){}};}};},setVelocity(x=0,y=0){current.vx=x;current.vy=y;current.dx=current.dy=0;},setDamping(x=0,y=0){current.dx=x;current.dy=y;},setVec2(x=0,y=0,dx=0,dy=0){current.sx=x;current.sy=y;current.sdx=dx;current.sdy=dy;},loseHp});
 const actorOwner=ownerKind===0?Object.assign(new FighterMain(),{musouEnemyData:ownerMinion?{isBoss:false}:null}):null;
 const h={...hurtMethods,_fighter:f,_action:action,_isDefense:guard,_hurtHoldFrame:0,_defenseHoldFrame:0,_beHitGap:0};
 const childOwner=ownerKind===2?Object.assign(new FighterAttacker(),{getOwner(){return this.root;},root:Object.assign(new FighterMain(),{musouEnemyData:ownerMinion?{isBoss:false}:null})}):null;
 const bulletOwner=ownerKind===1?Object.assign(new Bullet(),{owner:Object.assign(new FighterMain(),{musouEnemyData:ownerMinion?{isBoss:false}:null})}):null;
 const owner=actorOwner||childOwner||bulletOwner;
 const drawSeed=(0x9876f00d^scenario)>>>0;
 const c={f,h,drawState:drawSeed,dies:0,effectHash:2166136261,eventHash:2166136261,effects:0,animations:0,actions:0,pushes:0,reverseX:0,vx:0,vy:0,dx:0,dy:0,sx:0,sy:0,sdx:0,sdy:0,mainPlay:0,mainFrames:0,
  random(){let v=this.drawState;v^=v<<13;v^=v>>>17;v^=v<<5;this.drawState=v>>>0;return this.drawState/4294967296;},
  word(v){v>>>=0;for(let i=0;i<4;++i){this.eventHash=Math.imul((this.eventHash^((v>>>(i*8))&255))>>>0,16777619)>>>0;}},
  event(op){this.word(op);this.word(+f.isAlive);this.word(f.actionState);this.word(Math.round(f.hp*1000));},
  effect(type){++this.effects;this.effectHash=Math.imul((this.effectHash^type)>>>0,16777619)>>>0;this.event(type);}};current=c;
 if(owner)Object.assign(owner,{direct:scenario%2?-1:1,x:ownerX,isAllowReversePush:ownerReverse,setVec2(x,y,dx,dy){++c.pushes;c.reverseX=x;c.event(10);}});
 const mc={goFrame(name,play=true,frames=0){c.mainPlay=+play;c.mainFrames=frames;},playHurtFly(x,y){if(x)f.direct=x>0?-1:1;f.setVelocity(x,y);f.setDamping(0,.5);c.mainPlay=c.mainFrames=0;}};
 h._mc=mc;h._owner={effectCtrler:{endShadow(){},endShake(){}},setSteelBody(v){f.isSteelBody=f.isSuperSteelBody=v;},doAction,_rt:{fighter:f,action,mc},_hurtCtrl:{clearDefense(){h._isDefense=false;}},_actionCtrl:{resetDoActionFrame(){}}};
 f._fighterCtrl={getMcCtrl(){return h;}};GameMode.currentMode=training?GameMode.TRAINING:0;
 const originalHurt=h.doHurtAnimate;h.doHurtAnimate=function(...args){++c.animations;originalHurt.apply(h,args);};
 for(let serial=0;serial<2;++serial){
  const prelude=serial?pick([0,0,4,10]):0;for(let i=0;i<prelude;++i)h.renderBeHitGap();
  const damage=pick([0,1,2,17,99,500,2000]),power=damage/2,hitType=pick([0,1,2,3,4,5,6,7,8,9,10,11]),launch=bit(),breaks=bit(),bisha=bit(),gravity=bit(),rect=bit(),x=pick([-17,0,4,7,19]),y=pick([-9,0,5,8]),ms=pick([0,100,333,900]);
  const hit={...hitMethods,owner,hitType,checkDirect:true,hurtType:+launch,isBreakDef:breaks,targetApplyG:gravity,hitx:x,hity:y,hurtTime:ms,power,isBisha(){return bisha;},getDamage(){return damage;}};
  f.beHit(hit,rect?{}:null);
  const input=[hz,scenario,serial,hp,+alive,energy,maxEnergy,+overload,+guard,+steel,+superSteel,+allow,defRate,defense,+air,+side,ownerKind,+ownerMinion,+minion,+ownerReverse,facing,ownerX,+training,+hurtAction,drawSeed,prelude,damage,power,hitType,+launch,+breaks,+bisha,+gravity,+rect,x,y,ms];
  const out=[f.hp,+f.isAlive,f.energy,+f.energyOverLoad,f._energyAddGap,+f.isAllowBeHit,h._beHitGap,f.actionState,h._hurtHoldFrame,h._defenseHoldFrame,+h._isDefense,+action.isHurting,+action.isHurtFlying,+action.isDefenseHiting,+f.isApplyG,c.vx,c.vy,c.dx,c.dy,c.sx,c.sy,c.sdx,c.sdy,c.pushes,c.reverseX,c.dies,c.effects,c.effectHash,c.eventHash,c.animations,c.actions,c.mainPlay,c.mainFrames,f.qi,f._currentHurts?.length||0,c.drawState];
  for(const name of [!f.isAlive?'dead':'alive',action.isHurting?'hurt':action.isHurtFlying?'fly':action.isDefenseHiting?'guard':'other'])coverage[name]=(coverage[name]||0)+1;
  rows.push([...input,...out]);
 }
}
const report={rows:rows.length,inputColumns:37,outputColumns:36,sourceHashes:Object.fromEntries(Object.values(files).map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(path.join(reference,p))).digest('hex')])),coverage,scope:'Mechanically adapted original FighterMcCtrler.doAction, FighterMain.beHit/gauge helpers, BaseGameSprite.loseHp, FighterMcHurtCtrl.beHit/steel/guard/break/hurt/initial hurt animation and hit-gap methods, HitVO weak/catch predicates, HitType heavy predicate, GameLogic death check, FighterAction.clearState. Declared JS sprite classes/type adapter, empty super.beHit helper, original-type owner geometry, raw motion setters, MC entry, authored action frame/no-op voice/effect adapters and supplied uniform draws through a declared xorshift generator. Per-case two receipt calls with explicitly supplied renderBeHitGap calls; no full scene/MC animation or Flash VM proof.',trace:rows};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),rows.length+'\n'+rows.map(r=>r.join(' ')).join('\n')+'\n');console.log('Original full hit-controller receipts',rows.length,rows[0].length,coverage);
