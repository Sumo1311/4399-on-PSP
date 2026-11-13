// SPDX-License-Identifier: GPL-3.0-or-later
import fs from'node:fs';import path from'node:path';import crypto from'node:crypto';import{methodAdapter}from'./source_method_adapter.mjs';
const[reference,artFile,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE COMBO.json OUTPUT.json');
const k='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={logic:k+'ctrler/GameLogic.as',events:k+'ctrler/game_ctrls/FighterEventCtrl.as',musou:k+'ctrler/musou_ctrls/MusouLogic.as',ui:k+'ui/fight/HitsUI.as',number:'libraries/LIB_KyoLib/src/net/play5d/kyo/display/MCNumber.as',base:k+'interfaces/BaseGameSprite.as',data:k+'data/GameData.as'};
const original=Object.fromEntries(Object.entries(files).map(([key,p])=>[key,fs.readFileSync(path.join(reference,p),'utf8')]));
// Rename only the selected setter declaration so the shared method adapter can
// select it as an ordinary method; the executed setter body is unchanged.
const source={...original,number:original.number.replace('public function set number(v:uint):void','public function setNumber(v:uint):void')};
const art=JSON.parse(fs.readFileSync(artFile,'utf8')),glyphs=art.digits;
class BaseGameSprite{}class FighterMain extends BaseGameSprite{}
const GameLogic={},GameCtrl={I:{gameState:{gameUI:{getUI(){return versusUi;}}}}},GameUI={I:{getUI(){return musouUi;}}};
let score=0,updates=0,hitTargetCalls=0;const gameData={config:{keyInputMode:0}};Object.defineProperty(gameData,'score',{get(){return score;},set(v){score=v|0;}});
const GameData={I:gameData},GameEvent={SCORE_UPDATE:1,dispatchEvent(){++updates;}},FighterActionState={HURT_ING:21,HURT_FLYING:22};
const FighterEvent={HIT_TARGET:1},FighterEventDispatcher={dispatchEvent(){++hitTargetCalls;}};
const method=methodAdapter(source,{BaseGameSprite,FighterMain,GameLogic,GameCtrl,GameUI,GameData,GameEvent,FighterActionState,FighterEvent,FighterEventDispatcher,int:x=>Math.trunc(x)});
for(const name of['addHits','getHitsObj','getHitsObjByTargetId','clearHitsByTargetId','hitTarget','addScoreByHitTarget','addScore'])GameLogic[name]=method('logic',name);
const numberMethods={setNumber:method('number','setNumber'),createNum:method('number','createNum')},uiMethods={show:method('ui','show'),hide:method('ui','hide')};
class Digit {constructor(){this.x=0;this.digit=0;}gotoAndStop(frame){this.digit=frame-1;}}
function number(){
 const n={...numberMethods,_mc:Digit,_number:0,_bits:0,startFrame:1,mcWidth:35,_mcs:[],children:[],x:0,addChild(d){this.children.push(d);},removeChild(d){this.children.splice(this.children.indexOf(d),1);}};
 Object.defineProperty(n,'number',{get(){return n._number;},set(v){n.setNumber(v>>>0);}});
 Object.defineProperty(n,'width',{get(){return Math.max(...n.children.map(d=>d.x+glyphs[d.digit][3]+glyphs[d.digit][5]))-Math.min(...n.children.map(d=>d.x+glyphs[d.digit][3]));}});n.number=0;return n;
}
// The root clock and display properties are declared adapters. Script effects
// at the selected XFL frames are retained: stop at 1/20/38, loop 56 to 1.
function display(name,x,y){
 const mc={name,x,y,frame:1,visible:false,playing:false,gotoAndPlay(label){this.frame={fadin:10,update:30,fadout:51}[label];if(this.frame===10)this.visible=true;this.playing=true;}};
 return {...uiMethods,_txtmc:number(),_orgPos:{x,y},_mc:mc,_isShow:false,advance(){if(mc.playing){if(++mc.frame===56){mc.frame=1;mc.visible=mc.playing=false;}else if(mc.frame===20||mc.frame===38)mc.playing=false;}}};
}
let views=[];const versusUi={showHits(n,id){views[id-1].show(n);},hideHits(id){views[id-1].hide();}};
const musouUi={showHits(n){views[2].show(n);},hideHits(){views[2].hide();}};
const eventCtrl={addHits:method('events','addHits'),removeHits:method('events','removeHits')};
const mu={addHits:method('musou','addHits'),removeHitTarget:method('musou','removeHitTarget')};
let seed=0x491ec327;const random=()=>{seed^=seed<<13;seed^=seed>>>17;seed^=seed<<5;return(seed>>>0)/4294967296;},pick=a=>a[Math.floor(random()*a.length)];
const trace=[];let actors,targets;
for(let scenario=0;scenario<64;++scenario){
 GameLogic._hitsObj={};mu._hitTargets=[];mu._hitNum=0;score=updates=hitTargetCalls=0;
 views=[display('hits1',62.15,121.95),display('hits2',662.15,121.95),display('hitsmc',106.15,116.95)];
 actors=Array.from({length:4},(_,i)=>Object.assign(new FighterMain(),{id:'uuid-'+i,team:{id:i%2+1}}));targets=Array.from({length:8},(_,i)=>Object.assign(new FighterMain(),{id:'target-'+i}));
 for(let tick=0;tick<192;++tick){
  for(const v of views)v.advance();
  let op=pick([0,0,0,1,1,1,2,3,3,4,5,6,7,8]),a=pick([0,1,2,3]),b=pick([0,1,2,3,4,5,6,7]),c=pick([0,1,2]),value=pick([0,1,2,9,10,11,101,999,10000,4294967295]),power=pick([0,1,3.75,17,99,250.125,1500]),flags=Math.floor(random()*64);
  if(tick<2){op=1;a=0;b=0;c=0;}if(tick===2){op=5;c=0;value=101;}if(tick===3){op=6;c=0;}if(tick>=4&&tick<=14)op=0;
  if(op===1){c%=2;actors[a].team.id=c+1;eventCtrl.addHits(actors[a],targets[b]);}
  if(op===2)eventCtrl.removeHits(targets[b].id);
  if(op===3)mu.addHits(targets[b]);if(op===4)mu.removeHitTarget(targets[b]);
  if(op===5)views[c].show(value);if(op===6)views[c].hide();
  if(op===7){gameData.config.keyInputMode=+(!!(flags&32));GameLogic.addScoreByHitTarget({power,id:flags&2?'sh1':'zh1',isBreakDef:!!(flags&4),hurtType:+!!(flags&8),isBisha(){return !!(flags&1);}});}
  if(op===8){a%=3;b%=3;c=pick([0,1,16,17,20,21,22,23,24,30]);const f=a===0?new FighterMain():a===1?new BaseGameSprite():null,t=b===0?new FighterMain():b===1?new BaseGameSprite():null;if(t)t.actionState=c;GameLogic.hitTarget({},f,t);}
  const result=[];
  for(let i=0;i<4;++i){const e=GameLogic.getHitsObj(actors[i].id);result.push(e?e.hits:0,e?targets.findIndex(t=>t.id===e.targetID)+1:0,e?e.uiID:0);}
  result.push(mu._hitNum,mu._hitTargets.reduce((bits,t)=>bits|(1<<targets.indexOf(t)),0),mu._hitTargets.length);
  for(const v of views)result.push(v._txtmc.number,v._mc.frame,+v._isShow,+v._mc.visible,+v._mc.playing,v._txtmc.x,v._mc.x-v._orgPos.x,v._txtmc.width);
  result.push(score,updates,hitTargetCalls);trace.push([scenario,tick,op,a,b,c,value,power,flags,...result]);
 }
}
if(!trace.every(r=>r.length===51&&r.every(Number.isFinite)))throw Error('Nonfinite or incomplete original reference row');
const report={rows:trace.length,inputColumns:9,outputColumns:42,sourceHashes:Object.fromEntries(Object.values(files).map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(path.join(reference,p))).digest('hex')])),assetMetadataSha256:crypto.createHash('sha256').update(fs.readFileSync(artFile)).digest('hex'),scope:'Mechanically adapted GameLogic hit tables, HitTarget gate and score methods, FighterEventCtrl combo add/remove, MusouLogic shared target set, HitsUI show/hide, MCNumber setter/createNum. Stable fixture actor keys use a declared uuid- prefix; the original numeric score lookup does not match these string IDs. Declared object enumeration, sprite/type relations, Sprite bounds from pinned PNG leaf geometry, AS3 zero-initialized integer locals, typed score assignment, and selected root MovieClip stop/visibility/goto scripts. No full Flash VM/display-list/broadcast or source battle/raster trajectory proof.',trace};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),trace.length+'\n'+trace.map(r=>r.join(' ')).join('\n')+'\n');console.log('Original combo records',trace.length,trace[0].length);
