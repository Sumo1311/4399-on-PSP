// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference,output,counterOutput]=process.argv.slice(2);
if(!output)throw Error('REFERENCE OUTPUT.json');
const base='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={game:base+'ctrler/game_ctrls/GameCtrl.as',black:base+'ctrler/effect/EffectBlackBackHandler.as'};
const sources=Object.fromEntries(Object.entries(files).map(([k,v])=>[k,fs.readFileSync(path.join(reference,v),'utf8')]));
const GameConfig={FPS_GAME:60};
class Point{constructor(x,y){this.x=x;this.y=y;}}
const GameCtrl={I:null},SoundCtrl={I:{playSwcSound(){}}},GameLogic={fixGameSpritePosition(g){g.fixed++;}};
class BaseGameSprite{}
class FighterMain extends BaseGameSprite{}
const KeyBoarder={focus(){}},BitmapFilterView={},snd_ghost_jump={};
function method(file,name){
 const text=sources[file].replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
 const m=new RegExp('(?:public|private) function '+name+'\\(([^)]*)\\)(?:\\s*:\\w+)?\\s*\\{').exec(text);
 if(!m)throw Error(file+' '+name);
 let start=m.index+m[0].length,end=start,depth=1;
 for(;depth;++end){if(text[end]==='{')++depth;if(text[end]==='}')--depth;}
 const raw=text.slice(start,end-1),locals=[...raw.matchAll(/var\s+(\w+)\s*:/g)].map(v=>v[1]);
 const params=m[1].split(',').filter(Boolean).map(v=>v.trim().replace(/:[\w.]+/,''));
 const hidden=new Set([...locals,...params.map(v=>v.split('=')[0].trim())]);
 let index=0;
 const body=raw.replace(/for each\s*\(var (\w+):[\w.]+ in ([^)]+)\)\s*\{/g,(_,v,list)=>{
  const n='__forward'+index++;return `for(let ${n}=0;${n}<${list}.length;++${n}){var ${v}=${list}[${n}];`;
 }).replace(/var\s+(\w+):[\w.]+/g,'var $1').replace(/(\w+) is (\w+)/g,'$1 instanceof $2').replace(/\s+as\s+\w+/g,'');
 return Function('GameConfig','Point','GameCtrl','SoundCtrl','GameLogic','BaseGameSprite','FighterMain','KeyBoarder','BitmapFilterView','snd_ghost_jump','hidden',
  'return function('+params.join(',')+'){const self=this;const scope=new Proxy({},{has:(t,k)=>!hidden.has(k)&&Reflect.has(self,k),get:(t,k)=>Reflect.get(self,k,self),set:(t,k,v)=>Reflect.set(self,k,v,self)});with(scope){'+body+'}}')
  (GameConfig,Point,GameCtrl,SoundCtrl,GameLogic,BaseGameSprite,FighterMain,KeyBoarder,BitmapFilterView,snd_ghost_jump,hidden);
}
const blackMethods=Object.fromEntries(['bisha','wanKai','endBisha','endWanKai','ghostStep','endGhostStep',
 'replaceSkill','energyExplode','renderReplaceSkill','renderEnergyExplode','endReplaceSkill','endEnergyExplode','justRender','justRenderAnimate','cancelJustRender','cancelJustRenderAnimate','renderAnimate','render'].map(n=>[n,method('black',n)]));
const gameMethods=Object.fromEntries(['pause','resume','setRenderHit'].map(n=>[n,method('game',n)]));
const rows=[];
for(let sequence=0;sequence<729;++sequence)for(let targets=0;targets<8;++targets){
 let visible=true,focus=0,resume=0,animateCalls=0,renderCalls=0;
 const stage={getMap(){return {setVisible(v){visible=v;}};},addChildAt(){},setVisibleByClass(){},
  cameraFocusOne(){focus++;},cameraResume(){resume++;}};
 const game={...gameMethods,_gameRunning:true,_isRenderGame:true,_isPauseGame:false,
  _gameStartAndPause:false,_startCtrl:null,_endCtrl:null,_fightSession:null,_mainLogicCtrl:{renderHit:true},gameState:stage};
 GameCtrl.I=game;
 const black={...blackMethods,_ctrl:{doEffectById(){}},_gameStage:stage,
  _blackBack:{fadIn(){},fadOut(){},renderAnimate(){}},_renderBlackBack:false,
  _replaceSkillFrameHold:0,_explodeSkillFrame:0,_justRenderAnimateTargets:[],_justRenderTargets:[]};
 const actors=[0,1].map(id=>Object.assign(new FighterMain(),{id,fixed:0,end:3,getDisplay(){return {};},
  renderAnimate(){animateCalls|=1<<id;black[this.end===6?'endGhostStep':this.end===4?'endWanKai':'endBisha'](this);},
  render(){renderCalls|=1<<id;}}));
 const mask=list=>list.reduce((m,a)=>m|(1<<a.id),0);
 let value=sequence;
 for(let tick=0;tick<5;++tick){
  const op=tick<3?value%9:tick===3?9:10,key=tick<3?(targets>>tick)&1:0;
  if(tick<3)value=Math.floor(value/9);
  animateCalls=renderCalls=0;
  const actor=actors[key];
  switch(op){
  case 0:actor.end=3;black.bisha(actor,false);break;
  case 1:actor.end=3;black.bisha(actor,true);break;
  case 2:actor.end=4;black.wanKai(actor);break;
  case 3:black.endBisha(actor);break;
  case 4:black.endWanKai(actor);break;
  case 5:actor.end=6;black.ghostStep(actor);break;
  case 6:black.endGhostStep(actor);break;
  case 7:game.pause();break;
  case 8:game.resume();break;
  case 9:black.renderAnimate();break;
  case 10:black.render();break;
  }
  rows.push([sequence*8+targets,tick,op,key,+game._isRenderGame,+game._mainLogicCtrl.renderHit,
   mask(black._justRenderAnimateTargets),mask(black._justRenderTargets),+visible,focus,resume,animateCalls,renderCalls]);
 }
}
// Independent sanity checks catch a shadowed-parameter adapter before native use.
const caseRows=(sequence,targets)=>rows.filter(r=>r[0]===sequence*8+targets);
const bishaThenResume=caseRows(0+8*9+7*81,0);
if(bishaThenResume[1][4]!==1||bishaThenResume[1][6]!==1||bishaThenResume[2][4]!==0)throw Error('Flat flag sanity');
const twoCast=caseRows(0+0*9+7*81,2);
if(twoCast[3][11]!==1||twoCast[3][6]!==2||twoCast[3][4]!==0)throw Error('Forward removal sanity');
const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
const report={rows:rows.length,sequences:5832,fields:['case','tick','op','target','running','renderHit','animationMask','renderMask','mapVisible','cameraFocus','cameraResume','animationCalls','renderCalls'],
 sourceHashes:Object.fromEntries(Object.entries(files).map(([k,v])=>[v,sha(sources[k])])),
 scope:'Mechanically executes original GameCtrl pause/resume/setRenderHit and BlackBack selected-list, Bisha/WanKai/Ghost begin/end/render methods. Exhaustive three-operation sequences for two stable target objects, followed by selected animation/removal and logical render. Display, sound and actor callbacks are declared stubs. Mutable Vector for-each is adapted to a forward index with live length; this is a declared adapter, not Flash VM execution. Counter global timers, GameCtrl top-level round/session ordering and actual sprites are separate integration gates.',trace:rows};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');
fs.writeFileSync(output.replace(/\.json$/,'.IN'),[rows.length,...rows.map(r=>r.join(' '))].join('\n')+'\n');
console.log(JSON.stringify({rows:rows.length,sequences:5832,sourceHashes:report.sourceHashes}));
if(counterOutput){
 const counterRows=[];
 for(const hz of [30,60])for(let scenario=0;scenario<12;++scenario){
  GameConfig.FPS_GAME=hz;let visible=true,smokes=0,bursts=0;
  const stage={getMap(){return {setVisible(v){visible=v;}};},addChildAt(){},setVisibleByClass(){},cameraFocusOne(){},cameraResume(){}};
  const game={...gameMethods,_gameRunning:true,_isRenderGame:true,_isPauseGame:false,_gameStartAndPause:false,
   _startCtrl:null,_endCtrl:null,_fightSession:null,_mainLogicCtrl:{renderHit:true},gameState:stage};GameCtrl.I=game;
  const black={...blackMethods,_ctrl:{doEffectById(id){if(id==='replaceSp2')smokes++;if(id==='explodeSp2')bursts++;}},
   _gameStage:stage,_blackBack:{fadIn(){},fadOut(){},renderAnimate(){}},_renderBlackBack:false,
   _replaceSkillFrame:0,_replaceSkillFrameHold:0,_replaceSkillPos:null,_explodeSkillFrame:0,_explodeEffectPos:null,
   _justRenderAnimateTargets:[],_justRenderTargets:[]};
  for(const field of ['_replaceSkillFrame','_replaceSkillFrameHold','_explodeSkillFrame']){
   let value=0;Object.defineProperty(black,field,{get:()=>value,set:n=>{value=Math.trunc(n)|0;},enumerable:true});
  }
  const actors=[0,1].map(id=>Object.assign(new FighterMain(),{id,x:0,y:0,fixed:0,getDisplay(){return {};},render(){},renderAnimate(){}}));
  const commands=new Map(),put=(tick,...ops)=>commands.set(tick,ops);
  if(scenario===0)put(0,[7,0]);
  if(scenario===1)put(0,[8,0]);
  if(scenario===2){put(0,[7,0]);put(11,[7,1]);}
  if(scenario===3){put(0,[8,0]);put(11,[8,1]);}
  if(scenario===4){put(0,[7,0]);put(5,[8,1]);}
  if(scenario===5){put(0,[8,0]);put(5,[7,1]);}
  if(scenario===6){put(0,[0,0]);put(5,[7,1]);put(10,[3,0]);}
  if(scenario===7){put(0,[7,0]);put(5,[0,1]);put(10,[3,1]);}
  if(scenario===8){put(0,[5,0]);put(5,[8,1]);put(10,[6,0]);}
  if(scenario===9){put(0,[7,0]);put(5,[9,0]);put(10,[10,0]);}
  if(scenario===10)put(0,[7,0],[7,1]);
  if(scenario===11)put(0,[8,0],[7,1]);
  const mask=list=>list.reduce((n,a)=>n|(1<<a.id),0);
  for(let tick=0;tick<160;++tick){
   // Input requests arrive in MainLogic after this frame's BlackBack.render.
   black.render();const ops=commands.get(tick)||[];const inputs=[];
   for(let i=0;i<2;++i){
    if(i>=ops.length){inputs.push(-1,0,0,0);continue;}
    const [op,key]=ops[i],actor=actors[key];actor.x=100+key*200+tick*2;actor.y=400+key*50-tick;
    inputs.push(op,key,actor.x,actor.y);
    if(op===0)black.bisha(actor);if(op===3)black.endBisha(actor);
    if(op===5)black.ghostStep(actor);if(op===6)black.endGhostStep(actor);
    if(op===7)black.replaceSkill(actor);if(op===8)black.energyExplode(actor);
    if(op===9)game.pause();if(op===10)game.resume();
   }
   counterRows.push([hz,scenario,tick,...inputs,+game._isRenderGame,+game._mainLogicCtrl.renderHit,
    mask(black._justRenderAnimateTargets),mask(black._justRenderTargets),+visible,
    black._replaceSkillFrame,black._replaceSkillFrameHold,black._explodeSkillFrame,
    black._replaceSkillPos?.x||0,black._replaceSkillPos?.y||0,black._explodeEffectPos?.x||0,black._explodeEffectPos?.y||0,smokes,bursts]);
  }
 }
 const counterReport={rows:counterRows.length,scenarios:24,sourceHashes:report.sourceHashes,
  scope:'Mechanically executes original scene-global replace/explode start, render and expiry methods with AS3 int coercion, plus GameCtrl flat pause/resume and selected lists. Requests occur after BlackBack.render, as in MainLogic input. Effect creation, display, sound and selected actor render are declared callbacks; full effect definitions and complete Flash event/timeout dispatch are not executed.',trace:counterRows};
 fs.writeFileSync(counterOutput,JSON.stringify(counterReport,null,2)+'\n');
 fs.writeFileSync(counterOutput.replace(/\.json$/,'.IN'),[counterRows.length,...counterRows.map(r=>r.join(' '))].join('\n')+'\n');
 console.log(JSON.stringify({counterRows:counterRows.length,scenarios:24}));
}
