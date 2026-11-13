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

const moreFiles={hit:'upstream/CORE_KernelLogic/src/net/play5d/game/bvn/ctrler/effect/EffectHitHandler.as',fx:'upstream/CORE_KernelLogic/src/net/play5d/game/bvn/ctrler/EffectCtrl.as'};
const more=Object.fromEntries(Object.entries(moreFiles).map(([k,p])=>[k,fs.readFileSync(path.join(reference,p))]));
class IGameSprite{}
function extraMethod(key,name){
 const text=more[key].toString().replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
 const m=new RegExp(`public function ${name}\\(([^)]*)\\)(?:\\s*:\\w+)?\\s*\\{`).exec(text);if(!m)throw Error(name);
 let end=m.index+m[0].length,depth=1;const start=end;for(;depth;end++){if(text[end]==='{')depth++;if(text[end]==='}')depth--;}
 const body=text.slice(start,end-1).replace(/var (\w+):int\s*=\s*([^;]+);/g,'var $1=Math.trunc($2);').replace(/var (\w+):\w+/g,'var $1').replace(/([\w.]+)\s+is IGameSprite/g,'$1 instanceof IGameSprite').replace(/([\w.]+)\s+as IGameSprite/g,'$1');
 const args=m[1].split(',').filter(Boolean).map(x=>x.trim().replace(/:\w+/,''));
 return Function('Debugger','IGameSprite','GameCtrl','GameConfig',`return function(${args}){with(this){${body}}}`)({HIDE_HITEFFECT:false},IGameSprite,GameCtrl,GameConfig);
}
const freeze=extraMethod('fx','freeze'),doHitEffect=extraMethod('hit','doHitEffect');
const rows=[],records=[];
for(const hz of [30,60])for(const preset of [0,1.5,2])for(const resume of [0,1])for(const hitSlow of [0,500])for(const freezeMs of [200,400]){
 GameConfig.FPS_GAME=hz;GameCtrl.I.slowResume();let x=0,y=0,requests=0,paused=false;
 const map={getSmoothing:()=>({x,y}),setSmoothing(a,b){x=a;y=b;++requests;}};
 const slow={...methods,_gameStage:{getMap:()=>map},bgBlurEnabled:true,_blurFrame:0,_slowDownFrame:0,_renderAnimateGap:0,_renderAnimateFrame:0};
 const fx={freeze,freezeEnabled:true,_freezeFrame:0,_slowHandler:slow};GameCtrl.I.pause=()=>{paused=true;};
 const hit={doHitEffect,_hitFocusTarget:null,_manager:{getHitEffectVOByHitVO:()=>({followDirect:false,freeze:freezeMs})},
  _ctrl:{slowDown:(rate,time)=>slow.slowDown(rate,time),doEffectVO:effect=>fx.freeze(effect.freeze)}};
 if(preset)slow.slowDown(preset,1000);if(resume)slow.slowDownResume();
 hit.doHitEffect({owner:null,slowDown:hitSlow/1000,focusTarget:false},{x:0,y:0,width:20,height:20},null);
 rows.push([hz,preset,resume,hitSlow,freezeMs,x,y,GameCtrl.I.slowRate,slow._blurFrame,slow._slowDownFrame,requests,fx._freezeFrame,+paused].join(' '));
 records.push({hz,preset,resume,hitSlow,freezeMs,x,y,slowRate:GameCtrl.I.slowRate,blurFrames:slow._blurFrame,slowFrames:slow._slowDownFrame,requests,freezeFrames:fx._freezeFrame,paused});
}
fs.writeFileSync(output,rows.length+'\n'+rows.join('\n')+'\n');const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
fs.writeFileSync(output+'.json',JSON.stringify({rows:rows.length,records,sourceHashes:{[relative]:sha(raw),[gameRelative]:sha(gameRaw),...Object.fromEntries(Object.entries(moreFiles).map(([k,p])=>[p,sha(more[k])]))},fixtureSha256:sha(fs.readFileSync(output)),scope:'Original EffectHitHandler.doHitEffect, EffectCtrl.freeze, EffectSlowHandler and GameCtrl slow/restore methods. Manager supplies a declared effect with 200/400 ms freeze; doEffectVO callback invokes its configured freeze, other visual dispatch is outside this fixture. Map/camera/speed/paused callbacks are declared. Checks zero normal/restored rate, pre-hit slow-before-freeze ordering, priority rejection, retained blur after resume and accepted blur requests; not full engine clocks.'},null,2)+'\n');
console.log('Exported',rows.length,'original contact/blur ordering states');
