// SPDX-License-Identifier: GPL-3.0-or-later
// Original controller/effect scheduling with declared manager/display callbacks.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference,output]=process.argv.slice(2);if(!output)throw Error('reference OUTPUT.json');
const base='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={game:'ctrler/game_ctrls/GameCtrl.as',effects:'ctrler/EffectCtrl.as',slow:'ctrler/effect/EffectSlowHandler.as'};
const sources=Object.fromEntries(Object.entries(files).map(([key,name])=>[key,fs.readFileSync(path.join(reference,base+name))]));
const GameConfig={FPS_GAME:60,FPS_ANIMATE:30,CAMERA_TWEEN_SPD:5,get SPEED_PLUS_DEFAULT(){return this.FPS_ANIMATE/this.FPS_GAME;}};
const GameCtrl={I:null},EffectCtrl={I:null,BG_BULR_ENABLED:true};
function method(file,name){
 const text=sources[file].toString().replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
 const found=new RegExp(`(?:public|private) function ${name}\\(([^)]*)\\)(?:\\s*:\\w+)?\\s*\\{`).exec(text);if(!found)throw Error(file+' '+name);
 let end=found.index+found[0].length,depth=1;const start=end;
 for(;depth;++end){if(text[end]==='{')++depth;if(text[end]==='}')--depth;}
 const body=text.slice(start,end-1).replace(/var (\w+):\w+/g,'var $1');
 const args=found[1].split(',').filter(Boolean).map(a=>a.trim().replace(/:\w+/,''));
 return Function('GameConfig','GameCtrl','EffectCtrl',`return function(${args}){with(this){${body}}}`)(GameConfig,GameCtrl,EffectCtrl);
}
const methods={game:{},effects:{},slow:{}};
for(const name of ['render','checkRenderAnimate','renderAnimate','setAnimateFPS','slow','slowResume'])methods.game[name]=method('game',name);
for(const name of ['render','renderAnimate'])methods.effects[name]=method('effects',name);
for(const name of ['render','renderAnimate','isRenderAnimate','resetAnimateGap','slowDown','slowDownResume','bgBlur','cancelBgBlur'])methods.slow[name]=method('slow',name);
function typed(object,fields){for(const field of fields){let value=object[field]??0;Object.defineProperty(object,field,{get:()=>value,set:n=>{value=Math.trunc(n)|0;},enumerable:true});}return object;}
const rows=[];const cases=[];
for(const fps of [30,60])for(let scenario=0;scenario<5;++scenario){
 GameConfig.FPS_GAME=fps;let order=[],animation=false,actorCount=0,effectCount=0,timers=0,logicCount=0,mapCount=0,queued=false;
 let now=0;const smoothing={x:0,y:0};
 const map={getSmoothing:()=>smoothing,setSmoothing:(x,y)=>Object.assign(smoothing,{x,y})};
 const main={renderAnimate(){queued=true;order.push('queue-actor');},render(){animation=queued;queued=false;++logicCount;if(animation)++actorCount;order.push('logic');},setSpeedPlus(value){this.speed=value;}};
 const session={renderAnimate(){order.push('session-animation');},render(){order.push('session');},allowsRoundTimer:()=>true};
 const game=typed({...methods.game,_renderAnimateGap:Math.ceil(fps/30)-1,_renderAnimateFrame:0,_isPauseGame:false,_isRenderGame:true,
  _mainLogicCtrl:main,_fightSession:session,_startCtrl:null,_endCtrl:null,_trainingCtrl:{render(){order.push('training');}},
  actionEnable:true,_gameStartAndPause:false,slowRate:0,
  renderPause(){order.push('pause-input');},gameRunData:{setAllowLoseHP(){order.push('allow-hp');}},
  pause(){this._isPauseGame=true;this._isRenderGame=false;},
  _roundCtrl:{renderGameTime(){++timers;order.push('round-timer');},onEndFinished(){order.push('end-finished');}},
  gameState:{camera:{tweenSpd:5},getMap:()=>map,render(){++mapCount;order.push('stage');}}},['_renderAnimateGap','_renderAnimateFrame']);
 const slow=typed({...methods.slow,_renderAnimateGap:Math.ceil(fps/30)-1,_renderAnimateFrame:0,_slowDownFrame:0,_blurFrame:0,bgBlurEnabled:true,_gameStage:game.gameState},['_renderAnimateGap','_renderAnimateFrame','_slowDownFrame','_blurFrame']);
 const fx={...methods.effects,freezeEnabled:false,_slowHandler:slow,
  _effects:[{render(){order.push('effect-render');},renderAnimate(){++effectCount;order.push('effect-animation');}}],
  renderShine(){},clearFrameEffectCount(){},renderRemoveEnemy(){},_shadowHandler:{renderAnimate(){}},_shakeHandler:{renderAnimate(){}},
  _blackBackHandler:{render(){},renderAnimate(){}}};
 GameCtrl.I=game;EffectCtrl.I=fx;
 let differences=0;
 for(now=0;now<90;++now){
  order=[];animation=false;let action=0,rate=0,time=0;
  game._isPauseGame=scenario===2&&now>=7&&now<12;
  game._isRenderGame=!(scenario===1&&now>=5&&now<14);
  if(scenario===3){if(now===5){action=1;rate=1.5;time=300;}if(now===32){action=1;rate=2;}if(now===50){action=1;rate=1.25;time=500;}if(now===62)action=2;if(action===1)slow.slowDown(rate,time);if(action===2)slow.slowDownResume();}
  if(scenario===4){
   if(!now){game.actionEnable=false;game._startCtrl={render:()=>now===4,destroy(){order.push('start-destroy');}};}
   if(now===40){game.actionEnable=false;game._endCtrl={render:()=>now===44,destroy(){order.push('end-destroy');}};}
  }
  const before={actor:actorCount,effect:effectCount,logic:logicCount,map:mapCount,timers};
  game.render();
  const expectedGlobalTick=!game._isPauseGame&&game._isRenderGame&&(now+1)%Math.ceil(fps/30)===0;
  if(scenario===1&&animation!==expectedGlobalTick)++differences;
  rows.push({fps,scenario,tick:now,input:{manual:game._isPauseGame,logic:game._isRenderGame,action,rate,time},
   actorAnimation:animation,actorPhase:game._renderAnimateFrame,actorGap:game._renderAnimateGap,
   effectPhase:slow._renderAnimateFrame,effectGap:slow._renderAnimateGap,slowRate:game.slowRate,slowFrames:slow._slowDownFrame,
   counts:{actor:actorCount,effect:effectCount,logic:logicCount,map:mapCount,timers},before,order});
 }
 cases.push({fps,scenario,actorCount,effectCount,logicCount,mapCount,timers,globalTickModelDifferences:differences});
}
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const report={rows:rows.length,cases,sourceHashes:Object.fromEntries(Object.entries(files).map(([key,name])=>[base+name,sha(sources[key])])),
 scope:'Mechanically executed original GameCtrl render/checkRenderAnimate/renderAnimate/setAnimateFPS/slow/slowResume, EffectCtrl render/renderAnimate, and EffectSlowHandler scheduling methods. Declares int coercion, constant frame rates, display/manager/session/start/end callbacks and externally supplied logical/manual pause flags. Rows are original logical game ticks, not 60-Hz PSP display ticks. Global-tick comparison is an isolated modulo model, not a native gameplay trace. Actual freeze/cutin/input/physics and event timing remain separate integration work.',
 nativeIntegrationApplied:false,trace:rows};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');
const lines=[String(rows.length),...rows.map(r=>[r.fps,r.scenario,r.tick,r.input.action,r.input.rate,r.input.time,+r.input.manual,+r.input.logic,+r.actorAnimation,+!!(r.counts.effect-r.before.effect),r.actorGap,r.actorPhase,r.effectGap,r.effectPhase,r.slowRate,r.slowFrames].join(' '))];fs.writeFileSync(output.replace(/\.json$/,'.IN'),lines.join('\n')+'\n');
console.log('Exported',report.rows,'source controller/effect clock states',cases);
