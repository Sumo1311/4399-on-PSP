// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const [reference,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE OUTPUT.json');
const base='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={game:base+'ctrler/game_ctrls/GameCtrl.as',round:base+'ctrler/game_ctrls/GameRoundCtrl.as'};
const sources=Object.fromEntries(Object.entries(files).map(([key,name])=>[key,fs.readFileSync(path.join(reference,name),'utf8')]));
const GameConfig={FPS_GAME:30,FPS_ANIMATE:30},EffectCtrl={I:{render(){}}},GameCtrl={I:null},KeyBoarder={focus(){}};
function method(file,name){
 const text=sources[file].replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
 const match=new RegExp('(?:public|private) function '+name+'\\(([^)]*)\\)(?:\\s*:\\w+)?\\s*\\{').exec(text);if(!match)throw Error(file+' '+name);
 let begin=match.index+match[0].length,end=begin,depth=1;for(;depth;++end){if(text[end]==='{')++depth;if(text[end]==='}')--depth;}
 const raw=text.slice(begin,end-1),locals=[...raw.matchAll(/var\s+(\w+)\s*:/g)].map(row=>row[1]);
 const args=match[1].split(',').filter(Boolean).map(v=>v.trim().replace(/:[\w.]+/,''));
 const hidden=new Set([...locals,...args.map(v=>v.split('=')[0].trim())]);
 const body=raw.replace(/var\s+(\w+):[\w.]+/g,'var $1');
 return Function('GameConfig','EffectCtrl','GameCtrl','KeyBoarder','TraceLang','hidden',
  'return function('+args.join(',')+'){const self=this;const scope=new Proxy({},{has:(t,k)=>!hidden.has(k)&&Reflect.has(self,k),get:(t,k)=>Reflect.get(self,k,self),set:(t,k,v)=>Reflect.set(self,k,v,self)});with(scope){'+body+'}}')
  (GameConfig,EffectCtrl,GameCtrl,KeyBoarder,()=>{},hidden);
}
const gameMethods=Object.fromEntries(['render','checkRenderAnimate','renderAnimate','pause','resume'].map(name=>[name,method('game',name)]));
const roundMethods=Object.fromEntries(['renderGameTime','fightTimeover'].map(name=>[name,method('round',name)]));
const rows=[];
for(const hz of [30,60])for(const max of [1,2,-1])for(let scenario=0;scenario<4;++scenario){
 GameConfig.FPS_GAME=hz;let main=0,actors=0,uiTime=max,allowHp=true;
 const run={gameTimeMax:max,gameTime:max,isTimerOver:false,p1FighterGroup:{currentFighter:{hp:900}},p2FighterGroup:{currentFighter:{hp:scenario===3?900:700}}};
 const game={...gameMethods,_gameRunning:true,_isRenderGame:true,_isPauseGame:false,_gameStartAndPause:false,
  _renderAnimateGap:hz/30-1,_renderAnimateFrame:0,_startCtrl:null,_endCtrl:null,_trainingCtrl:null,
  actionEnable:true,roundTimeFrame:0,gameRunData:run,renderPause(){},
  _mainLogicCtrl:{renderAnimate(){actors++;},render(){main++;}},
  gameState:{render(){uiTime=run.gameTime;}},
  _fightSession:{allowsRoundTimer(){return true;},renderAnimate(){},render(){}},
  gameEnd(){this._endCtrl={render(){return false;}};allowHp=false;},
  drawGame(){this._endCtrl={render(){return false;}};allowHp=false;}};
 game._roundCtrl={...roundMethods,_owner:game};GameCtrl.I=game;
 for(let tick=0;tick<128;++tick){
  let op=0;
  if(scenario===1)op=tick===8?1:tick===19?2:0;
  if(scenario===2)op=tick===30?1:tick===43?2:0;
  if(scenario===3)op=[3,5,29,64].includes(tick)?1:[4,9,37,72].includes(tick)?2:0;
  if(op===1)game.pause();if(op===2)game.resume();game.render();
  rows.push([hz,max,scenario,tick,op,run.gameTime,game.roundTimeFrame,uiTime,+game._isRenderGame,+game.actionEnable,+allowHp,main,actors,game._renderAnimateFrame]);
 }
}
// Timer=30 is still live; timer=31 expires before the same callback's MainLogic.
const first=rows.filter(r=>r[0]===30&&r[1]===1&&r[2]===0);
if(first[29][5]!==1||first[30][5]!==0||first[30][7]!==1||first[30][11]!==31||first[31][7]!==0)throw Error('Source ordering sanity');
const report={rows:rows.length,scenarios:24,fields:['hz','max','scenario','tick','operation','seconds','roundTimeFrame','uiSeconds','running','actionEnabled','allowHp','mainCalls','actorAnimations','actorPhase'],
 sourceHashes:Object.fromEntries(Object.values(files).map(name=>[name,crypto.createHash('sha256').update(fs.readFileSync(path.join(reference,name))).digest('hex')])),
 scope:'Mechanically executes original GameCtrl.render/checkRenderAnimate/renderAnimate/pause/resume and GameRoundCtrl.renderGameTime/fightTimeover. Begins after the start controller has completed, at a normal actor phase of zero; covers 30/60 Hz, one/two/infinite seconds, flat pauses/resumes and draw/time-over. Effect, stage/UI, actual sprites, sessions and end-controller bodies are declared callbacks. Records UI time before timer mutation and continued MainLogic on the expiry callback. Not Flash VM, start/end timeline or detached-listener dispatch.',trace:rows};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),rows.length+'\n'+rows.map(row=>row.join(' ')).join('\n')+'\n');console.log('Original round states',rows.length);
