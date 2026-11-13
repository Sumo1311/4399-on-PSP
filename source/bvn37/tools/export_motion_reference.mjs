// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference,banks,output]=process.argv.slice(2);
if(!output)throw Error('REFERENCE BANKS OUTPUT.json');
const base='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={
 sprite:base+'interfaces/BaseGameSprite.as',logic:base+'ctrler/GameLogic.as',
 main:base+'ctrler/game_ctrls/GameMainLogicCtrl.as',floor:base+'map/FloorVO.as',
 config:base+'GameConfig.as',bullet:base+'fighter/Bullet.as',
 math:'libraries/LIB_KyoLib/src/net/play5d/kyo/utils/KyoMath.as'};
const texts=Object.fromEntries(Object.entries(files).map(([k,v])=>[k,fs.readFileSync(path.join(reference,v),'utf8')]));
const GameConfig={FPS_GAME:60,FPS_ANIMATE:30,
 get SPEED_PLUS_DEFAULT(){return this.FPS_ANIMATE/this.FPS_GAME;},SPEED_PLUS:.5};
for(const k of ['G','G_ADD','G_ON_FLOOR','NO_TOUCH_BAN_ON_VECY','X_SIDE_OFFSET']){
 const m=texts.config.match(new RegExp('(?:const|var) '+k+':(?:Number|int)\\s*=\\s*([^;]+);'));
 if(!m||!Number.isFinite(Number(m[1])))throw Error('Non-literal config '+k);
 GameConfig[k]=Number(m[1]);
}
const KyoMath={},GameLogic={},MCUtils={hasFrameLabel(mc,name){return name==='timeout'&&mc.timeout!==65535;}};
const EffectCtrl={I:{shake(){throw Error('Unexpected floor effect in this isolated bullet fixture');}}};
class BaseGameSprite{}
class FighterMain extends BaseGameSprite{}
function method(file,name){
 const text=texts[file].replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
 const re=new RegExp('(?:public|private)\\s+(?:static\\s+)?function '+name+'\\(([^)]*)\\)(?:\\s*:\\w+)?\\s*\\{');
 const m=re.exec(text);if(!m)throw Error(file+' '+name);
 let start=m.index+m[0].length,end=start,depth=1;
 for(;depth;++end){if(text[end]==='{')++depth;if(text[end]==='}')--depth;}
 const raw=text.slice(start,end-1);
 const localNames=[...raw.matchAll(/var\s+(\w+)\s*:/g)].map(m=>m[1]);
 const body=raw.replace(/var\s+(\w+):[\w.]+/g,'var $1')
  .replace(/\s+as\s+\w+/g,'').replace(/(\w+)\s+is\s+(\w+)/g,'$1 instanceof $2');
 const args=m[1].split(',').filter(Boolean).map(v=>v.trim().replace(/:[\w.]+/,'')).join(',');
 const hidden=new Set([...localNames,...m[1].split(',').filter(Boolean).map(v=>v.trim().split(':')[0])]);
 // AS3 parameters/locals shadow instance properties. A bare with(this) would
 // incorrectly resolve move(x,y) to the sprite's x/y properties instead.
 return Function('GameConfig','KyoMath','GameLogic','BaseGameSprite','FighterMain','MCUtils','EffectCtrl','hidden',
  'return function('+args+'){const self=this;const scope=new Proxy({},{has:(target,key)=>!hidden.has(key)&&Reflect.has(self,key),get:(target,key)=>Reflect.get(self,key,self),set:(target,key,value)=>Reflect.set(self,key,value,self)});with(scope){'+body+'}}')(GameConfig,KyoMath,GameLogic,BaseGameSprite,FighterMain,MCUtils,EffectCtrl,hidden);
}
KyoMath.weaken=method('math','weaken');
for(const n of ['isInAir','fixGameSpritePosition','isTouchBottomFloor','isOutRange'])GameLogic[n]=method('logic',n);
const spriteMethods=Object.fromEntries(['move','setSpeedRate','setVelocity','setDamping','setVec2','applayG','setInAir','renderVelocity','render'].map(n=>[n,method('sprite',n)]));
const floorMethod=method('floor','hitTest');
const floors=[{xFrom:120,xTo:280,y:300},{xFrom:260,xTo:380,y:300}].map(f=>({...f,hitTest:floorMethod}));
const map={left:0,right:1200,bottom:640,playerBottom:600,getFloorHitTest(x,y,range){return floors.find(f=>f.hitTest(x,y,range))||null;}};
function sprite(){
 const s=Object.assign(new FighterMain(),spriteMethods,{
  _x:200,_y:100,_velocity:{x:0,y:0},_velocity2:{x:0,y:0},_damping:{x:0,y:0},_damping2:{x:0,y:0},_g:0,
  _speedPlus:GameConfig.SPEED_PLUS_DEFAULT,_dampingRate:1,_destroyed:false,_mainMc:{x:0,y:0},
  isApplyG:true,isInAir:false,isTouchBottom:false,_side:false,crossSide:false,crossBottom:false,
  renderFrameOut(){},getVecX(){return this._velocity.x;},getVecY(){return this._velocity.y;},
  setIsTouchSide(v){this._side=v;},allowCrossMapXY(){return this.crossSide;},allowCrossMapBottom(){return this.crossBottom;}});
 Object.defineProperties(s,{x:{get(){return this._x;},set(v){this._x=v;}},y:{get(){return this._y;},set(v){this._y=v;}}});
 return s;
}
const motion=[];
for(const hz of [30,60])for(let scenario=0;scenario<12;++scenario)for(let variant=0;variant<3;++variant){
 GameConfig.FPS_GAME=hz;const s=sprite();
 GameLogic._map=map;GameLogic._floorContact={};
 GameLogic._camera={autoZoomMin:1,getZoom:()=>scenario===8?1:2,getScreenRect:()=>({x:110,y:0,width:400,height:600})};
 for(let tick=0;tick<96;++tick){
  const a={flags:0,x:0,y:0,vx:0,vy:0,dx:0,dy:0,v2x:0,v2y:0,d2x:0,d2y:0,
   gravity:scenario!==3,crossSide:scenario===7&&tick>=20&&tick<40,crossBottom:scenario===7&&tick>=30&&tick<50,camera:scenario===8};
  let rate=[1,1.5,2][variant];if(scenario===10&&tick>=48)rate=1;
  if(scenario===10&&tick>=20&&tick<40)a.gravity=false;
  if(!tick){
   a.flags=15;a.x=200;a.y=100;
   if(scenario===0){a.y=600;a.vx=8;}
   if(scenario===1){a.vx=2;a.dx=.1;a.dy=.5;a.v2x=-5;a.v2y=2;a.d2x=.2;a.d2y=.1;}
   if(scenario===2){a.y=600;a.vy=-12;a.dy=.5;}
   if(scenario===3){a.vx=-8;a.vy=-4;a.dx=-.2;a.dy=-.5;}
   if(scenario===4){a.y=294;}
   if(scenario===5){a.y=300;a.vy=5*GameConfig.SPEED_PLUS_DEFAULT/rate;}
   if(scenario===6){a.y=590;a.vy=3;}
   if(scenario===7){a.x=0;a.y=590;a.vx=-4;a.vy=8;a.dx=.1;}
   if(scenario===8){a.x=110;a.vx=-4;}
   if(scenario===9){a.v2x=-4;a.v2y=-3;a.d2x=-.3;a.d2y=.2;}
   if(scenario===11){a.vx=10;a.vy=-10;a.dx=.3;a.dy=.2;}
  }
  if(scenario===2&&tick===18){a.flags|=8;a.v2x=4;a.v2y=-2;a.d2x=.2;a.d2y=.3;}
  if(scenario===4&&[8,16,24,26].includes(tick)){a.flags|=1;a.x={8:275,16:290,24:380,26:260}[tick];a.y=300;}
  if(scenario===5&&[1,2,3].includes(tick)){a.flags|=3;a.x=200;a.y=300;a.vy=5*GameConfig.SPEED_PLUS_DEFAULT/rate+(tick===1?-.01:tick===2?.01:0);}
  if(scenario===6&&tick===20){a.flags|=6;a.vy=-1;a.dy=.2;}
  if(scenario===8&&tick===20){a.flags|=2;a.vx=12;}
  if(scenario===8&&tick===40){a.flags|=3;a.x=700;a.y=100;}
  if(scenario===8&&tick===60){a.flags|=2;a.vx=-6;}
  if(scenario===11&&tick===10){a.flags|=2;a.vx=5;a.vy=0;}
  if(scenario===11&&tick===20){a.flags|=4;a.dx=.2;a.dy=-.2;}
  GameConfig.SPEED_PLUS=GameConfig.SPEED_PLUS_DEFAULT/rate;s.setSpeedRate(GameConfig.SPEED_PLUS);
  if(a.flags&1){s.x=a.x;s.y=a.y;}if(a.flags&2)s.setVelocity(a.vx,a.vy);
  if(a.flags&4)s.setDamping(a.dx,a.dy);if(a.flags&8)s.setVec2(a.v2x,a.v2y,a.d2x,a.d2y);
  s.isApplyG=a.gravity;s.crossSide=a.crossSide;s.crossBottom=a.crossBottom;
  GameLogic.fixGameSpritePosition(s);const air=GameLogic.isInAir(s);if(air)s.applayG(GameConfig.G);s.setInAir(air);s.render();
  motion.push([hz,scenario,variant,tick,rate,a.flags,a.x,a.y,a.vx,a.vy,a.dx,a.dy,a.v2x,a.v2y,a.d2x,a.d2y,
   +a.gravity,+a.crossSide,+a.crossBottom,+a.camera,s.x,s.y,s._velocity.x,s._velocity.y,s._velocity2.x,s._velocity2.y,s._g,+s.isInAir,+s._side,+s.isTouchBottom,s._damping.x,s._damping.y]);
 }
}
function bank(name){
 const filename=path.join(banks,name),fd=fs.openSync(filename,'r'),h=Buffer.alloc(54);fs.readSync(fd,h,0,54,0);
 if(!/^BVNFGT1[45]$/.test(h.subarray(0,8).toString()))throw Error('Unsupported packed version '+name);
 const b=Buffer.alloc(h.readUInt32LE(20));fs.readSync(fd,b,0,b.length,0);fs.closeSync(fd);
 const fc=h.readUInt32LE(8),hc=h.readUInt32LE(12),ec=h.readUInt32LE(16),cc=h.readUInt16LE(24),sc=h.readUInt16LE(26),n=h.readUInt16LE(40);
 const at=54+432+cc*8+fc*48+hc*20+sc*24+ec*12;
 const defs=Array.from({length:n},(_,i)=>{const o=at+i*92,f=j=>b.readFloatLE(o+j*4),u=j=>b.readUInt16LE(o+64+j*2);return {
  x:f(0),y:f(1),xaxis:[6,7,8,9,10].map(f),yaxis:[11,12,13,14,15].map(f),
  initial:u(2),timeout:u(6),life:h.readInt16LE.call(b,o+84),owner:b.readFloatLE(o+88)};});
 return {name,defs,metadataSha256:crypto.createHash('sha256').update(b).digest('hex')};
}
const names=['ICHIGO.BVF','CHAD.BVF','DEIDARAO.BVF','DEIDARA.BVF','BYAKUYBO.BVF'];
const assets=names.map(bank),choices=[[0,0],[0,2],[1,0],[2,1],[3,2],[4,0]],bullets=[];
const renderBullet=method('bullet','render');
GameLogic._map={left:-12000,right:12000,bottom:600,playerBottom:560};
for(const hz of [30,60])for(let choice=0;choice<choices.length;++choice)for(const facing of [-1,1])for(let variant=0;variant<3;++variant){
 GameConfig.FPS_GAME=hz;const [asset,index]=choices[choice],d=assets[asset].defs[index];
 const state={active:true,_isHit:false,_isTimeout:false,_direct:facing,_speedPlus:GameConfig.SPEED_PLUS_DEFAULT,
  speed:{x:d.xaxis[0]*facing,y:d.yaxis[0]},addSpeed:{x:d.xaxis[1]*2*facing,y:d.yaxis[1]*2},
  minSpeed:{x:d.xaxis[2],y:d.yaxis[2]},maxSpeed:{x:d.xaxis[3],y:d.yaxis[3]},
  holdFrame:d.life===-1?-1:Math.trunc(d.life*hz/60),
  mc:{x:500+d.owner+d.x*facing,y:-4000+d.y,phase:d.initial,timeout:d.timeout,
   gotoAndStop(label){if(label!=='timeout')throw Error(label);this.phase=this.timeout;}},
  removeSelf(){this.active=false;},get x(){return this.mc.x;},get y(){return this.mc.y;},render:renderBullet};
 for(let tick=0;tick<96;++tick){
  let rate=[1,1.5,2][variant];if(tick>=48)rate=1;
  state._speedPlus=GameConfig.SPEED_PLUS_DEFAULT/rate;state.mc.y=Math.min(state.mc.y,600);
  if(state.active)state.render();
  bullets.push([hz,choice,facing,variant,tick,asset,index,rate,state.x,state.y,state.speed.x,state.speed.y,
   +state.active,state.mc.phase]);
 }
}
if(motion[0][20]!==208||motion[0][21]!==600||motion[0][30]!==0)throw Error('Parameter-scope sanity check failed');
if(![...motion,...bullets].every(row=>row.every(Number.isFinite)))throw Error('Non-finite source state');
const sha=s=>crypto.createHash('sha256').update(s).digest('hex');
const report={motionRows:motion.length,bulletRows:bullets.length,motionColumns:32,bulletColumns:14,
 sourceHashes:Object.fromEntries(Object.entries(files).map(([k,n])=>[n,sha(texts[k])])),
 bankBindings:assets.map(a=>({name:a.name,metadataSha256:a.metadataSha256})),choices,
 scope:'Mechanically executes original BaseGameSprite setters, gravity and velocity/render, GameLogic geometry/floor checks, FloorVO.hitTest, KyoMath.weaken, and Bullet.render. Declares sprite/point/camera/map adapters, original numeric GameConfig fields, single-target dictionary, no scheduled frame callbacks, and pre-bound packed bullet data/timeout labels. Steps are original 30/60-Hz logical callbacks, with declared normal/slow/resume inputs. Bullet trajectories use existing packed float parameters and far-from-floor placements, not raw-AS3 parameter precision, all bullet hits/animation, original VM/raster or full combat ordering.',
 motion,bullets};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');
fs.writeFileSync(output.replace(/\.json$/,'.MOTION.IN'),[motion.length,...motion.map(r=>r.join(' '))].join('\n')+'\n');
fs.writeFileSync(output.replace(/\.json$/,'.BULLET.IN'),[names.length,...names,bullets.length,...bullets.map(r=>r.join(' '))].join('\n')+'\n');
console.log('Exported source physical states',motion.length,bullets.length);

