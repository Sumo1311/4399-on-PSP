// Run pinned EffectSlowHandler blur and slow-state methods in declared map stubs.
// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference,output]=process.argv.slice(2);if(!output)throw Error('usage: export_blur_reference.mjs reference BLUR.IN');
const relative='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/ctrler/effect/EffectSlowHandler.as';
const raw=fs.readFileSync(path.join(reference,relative));
const source=raw.toString().replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
const GameConfig={FPS_GAME:30,FPS_ANIMATE:30,CAMERA_TWEEN_SPD:2.5,get SPEED_PLUS_DEFAULT(){return this.FPS_ANIMATE/this.FPS_GAME;}},EffectCtrl={BG_BULR_ENABLED:true},GameCtrl={I:null};
const gameRelative='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/ctrler/game_ctrls/GameCtrl.as';
const gameRaw=fs.readFileSync(path.join(reference,gameRelative));
const gameSource=gameRaw.toString().replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
function gameMethod(name){
 const m=new RegExp(`(?:public|private) function ${name}\\(([^)]*)\\)(?:\\s*:\\w+)?\\s*\\{`).exec(gameSource);if(!m)throw Error(name);
 let end=m.index+m[0].length,depth=1;const start=end;for(;depth;end++){if(gameSource[end]==='{')depth++;if(gameSource[end]==='}')depth--;}
 const body=gameSource.slice(start,end-1).replace(/var (\w+):\w+/g,'var $1'),args=m[1].split(',').filter(Boolean).map(a=>a.trim().replace(/:\w+/,''));
 return Function('GameConfig',`return function(${args}){with(this){${body}}}`)(GameConfig);
}
GameCtrl.I={slowRate:0,_renderAnimateGap:0,_renderAnimateFrame:0,_mainLogicCtrl:{setSpeedPlus(){}},gameState:{camera:{tweenSpd:2.5}},slow:gameMethod('slow'),slowResume:gameMethod('slowResume'),setAnimateFPS:gameMethod('setAnimateFPS')};
const methods={};
for(const name of ['bgBlur','cancelBgBlur','slowDown','slowDownResume','render','renderAnimate','resetAnimateGap']){
 const m=new RegExp(`public function ${name}\\(([^)]*)\\)(?:\\s*:\\w+)?\\s*\\{`).exec(source);if(!m)throw Error(name);
 let end=m.index+m[0].length,depth=1;const start=end;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}
 let body=source.slice(start,end-1).replace(/var (\w+):\w+/g,'var $1');
 // AS3 assignments into these int fields truncate toward zero.
 body=body.replace(/(_blurFrame|_slowDownFrame)\s*=\s*([^;]+);/g,'$1=Math.trunc($2);');
 const args=m[1].split(',').filter(Boolean).map(a=>a.trim().replace(/:\w+/,''));
 methods[name]=Function('GameConfig','EffectCtrl','GameCtrl',`return function(${args}){with(this){${body}}}`)(GameConfig,EffectCtrl,GameCtrl);
}
const rows=[];
for(const hz of [30,60])for(let variant=0;variant<4;variant++){
 GameConfig.FPS_GAME=hz;GameCtrl.I.slowResume();let x=0,y=0;
 const map={getSmoothing:()=>({x,y}),setSmoothing(a,b){x=a;y=b;}};
 const c={...methods,_gameStage:{getMap:()=>map},bgBlurEnabled:true,_blurFrame:0,_slowDownFrame:0,_renderAnimateGap:0,_renderAnimateFrame:0};
 for(let tick=0;tick<240;tick++){
  let action=0,a=0,b=0,time=0;
  if(tick===0){action=1;a=4;time=500;}
  if(tick===4){action=1;a=3;time=1200;}
  if(tick===8){action=2;a=1.5;time=1000;}
  if(tick===16){action=1;a=6;time=250;}
  if(tick===20){action=3;a=0;}
  if(tick===24){action=1;a=10;time=2000;}
  if(tick===28){action=3;a=1;}
  if(tick===32){action=1;a=10;time=2000;}
  if(tick===36){action=2;a=2;time=0;}
  if(tick===44){action=2;a=1.5;time=250;}
  if(tick===72){action=4;}
  if(tick===80){action=1;a=4.5;b=variant===1?2:0;time=100;}
  if(tick===84){action=1;a=8;time=0;}
  if(tick===100){action=4;}
  if(tick===110){action=1;a=variant===2?0.5:3;time=variant===3?-100:250;}
  if(tick===140){action=4;}
  if(tick===150){action=2;a=2;time=50;}
  if(action===1)c.bgBlur(a,b,time);
  if(action===2)c.slowDown(a,time);
  if(action===3)c.bgBlurEnabled=!!a;
  if(action===4)c.cancelBgBlur();
  const render=(tick+1)*hz%60===0,animate=tick%2===0&&!(variant===2&&tick>=48&&tick<70);
  if(render)c.render();if(animate)c.renderAnimate();
  rows.push([hz,variant,tick,+render,+animate,action,a,b,time,x,y,c._blurFrame,GameCtrl.I.slowRate,c._slowDownFrame,+c.bgBlurEnabled].join(' '));
 }
}
fs.writeFileSync(output,rows.length+'\n'+rows.join('\n')+'\n');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
fs.writeFileSync(output+'.json',JSON.stringify({rows:rows.length,sourceHashes:{[relative]:sha(raw),[gameRelative]:sha(gameRaw)},fixtureSha256:sha(fs.readFileSync(output)),scope:'Original EffectSlowHandler blur/slow methods after declared AS3 int/type adapters and a map adapter plus original GameCtrl slow/slowResume/setAnimateFPS methods with declared camera/speed callbacks. Animation and game render calls are supplied inputs. Covers rejection, enable gating, stronger blur, vertical locks, cancellation, fractional widths, zero/negative holds, slow precedence and indefinite slow. Does not prove full original controller scheduling or filter raster.'},null,2)+'\n');
console.log('Exported blur controller reference states',rows.length);
