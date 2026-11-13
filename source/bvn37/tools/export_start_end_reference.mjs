// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const [reference,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE OUTPUT.json');
const base='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={start:base+'ctrler/game_ctrls/GameStartCtrl.as',end:base+'ctrler/game_ctrls/GameEndCtrl.as'};
const source=Object.fromEntries(Object.entries(files).map(([k,v])=>[k,fs.readFileSync(path.join(reference,v),'utf8')]));
const GameConfig={FPS_GAME:30},FighterActionState={KAI_CHANG:60,isAllowWinState:s=>![12,13,50].includes(s)};
let event=0;const mark=n=>event=(Math.imul(event,23)+n)>>>0;
const StateCtrl={I:{transOut(cb){mark(cb?7:8);callbacks.transition=cb;},quickTrans(){mark(9);},transIn(cb){mark(14);callbacks.transition=cb;}}};
const SoundCtrl={I:{smartPlayGameBGM(){}}},GameMode={isTeamMode:()=>false,currentMode:0,SURVIVOR:1};
const run={lastWinner:{hp:700,hpMax:1000},gameTime:30,gameTimeMax:60,setAllowLoseHP(){}};
const GameCtrl={I:{gameRunData:run}},callbacks={transition:null,ui:null};
const ui={showStart(cb){mark(6);callbacks.ui=cb;},fadIn(){mark(10);},showEnd(cb){mark(12);callbacks.ui=cb;},clearStartAndEnd(){mark(15);},fadOut(){mark(16);}};
const GameUI={I:{getUI:()=>ui}};
function method(file,name){
 const text=source[file].replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
 const match=new RegExp('(?:public|private) function '+name+'\\(([^)]*)\\)(?:\\s*:[\\w.]+)?\\s*\\{').exec(text);if(!match)throw Error(name);
 let begin=match.index+match[0].length,end=begin,depth=1;for(;depth;++end){if(text[end]==='{')++depth;if(text[end]==='}')--depth;}
 const raw=text.slice(begin,end-1),locals=[...raw.matchAll(/var\s+(\w+)\s*:/g)].map(x=>x[1]);
 const args=match[1].split(',').filter(Boolean).map(x=>x.trim().replace(/:[\w.]+/,''));
 const hidden=new Set([...locals,...args.map(x=>x.split('=')[0].trim())]);
 const body=raw.replace(/var\s+(\w+):[\w.]+/g,'var $1').replace(/function\s*\(\)\s*:\s*void/g,'function()');
 return Function('GameConfig','FighterActionState','StateCtrl','SoundCtrl','GameUI','GameCtrl','GameMode','hidden',
 'return function('+args.join(',')+'){const self=this;const scope=new Proxy({},{has:(t,k)=>!hidden.has(k)&&Reflect.has(self,k),get:(t,k)=>{const v=Reflect.get(self,k,self);return typeof v===\'function\'?v.bind(self):v},set:(t,k,v)=>Reflect.set(self,k,v,self)});with(scope){'+body+'}}')
 (GameConfig,FighterActionState,StateCtrl,SoundCtrl,GameUI,GameCtrl,GameMode,hidden);
}
const starts=Object.fromEntries(['render','start1v1','startNextRound','startMusou','skip','renderStartMusou','preRenderStart','renderStart1v1','renderNextRound'].map(n=>[n,method('start',n)]));
const ends=Object.fromEntries(['initialize','drawGame','render','skip','renderDrawGame','renderEND'].map(n=>[n,method('end',n)]));
const rows=[];
for(const hz of [30,60])for(const kind of [0,1,2,3,4])for(const team of (kind===0?[-1,1,2]:[-1]))for(let scenario=0;scenario<6;++scenario){
 GameConfig.FPS_GAME=hz;callbacks.transition=callbacks.ui=null;event=0;
 const p1={actionState:0,data:{id:1},getDisplay:()=>0,sayIntro(){mark(4);},idle(){mark(11);},win(){mark(13);}};
 const p2={...p1,data:{id:2},getDisplay:()=>1,sayIntro(){mark(5);},idle(){}};
 const state={cameraFocusOne:side=>mark(side+1),cameraResume:()=>mark(3),camera:{updateNow(){}},gameUI:{getUI:()=>ui}};
 let c,done=false;
 if(kind<3){c={...starts,_state:state,_p1:p1,_p2:p2,_isStart1v1:false,_isStartNextRound:false,_isStartMusou:false,_step:0,_holdFrame:0,_uiPlaying:false,_introTeamId:-1,_musouFinish:false};
  if(kind===0)c.start1v1(p1,p2,team);else if(kind===1)c.startNextRound();else c.startMusou();
 }else{c={...ends,SHOW_CONTINUE:false,_winner:null,_loser:null,_step:0,_holdFrame:0,_isRender:false,_drawGame:false};if(kind===3)c.initialize(p1,p2);else c.drawGame();}
 event=0;
 for(let tick=0;tick<800;++tick){
  const skip=scenario===1?tick===2:scenario===2?tick===35:scenario===3?tick%7===0:scenario===4?tick===200||tick===450:false;
  const transition=!!callbacks.transition&&(tick===25||tick===380||tick===650);
  const uiEvent=!!callbacks.ui&&(scenario!==5)&&(tick===120||tick===240||tick===500);
  const intro=(tick>=44&&tick<57)||(scenario===4&&tick>=73&&tick<101);
  const allow=!(scenario===4&&tick>=120&&tick<300);
  event=0;let returned=false;
  if(transition){const cb=callbacks.transition;callbacks.transition=null;cb();}
  if(uiEvent){const cb=callbacks.ui;callbacks.ui=null;cb();}
  if(!done){p1.actionState=intro?60:allow?0:12;
   if(skip)c.skip();returned=c.render();if(returned)done=true;
  }
  rows.push([kind,hz,team,scenario,tick,+skip,+transition,+uiEvent,+intro,+allow,+returned,c._step,c._holdFrame,+(c._uiPlaying||false),+(c._isRender||false),event]);
 }
}
const names=['kind','hz','team','scenario','tick','skip','transitionComplete','uiComplete','introPlaying','allowWin','returned','step','hold','uiPlaying','endActive','events'];
const report={rows:rows.length,fields:names,sourceHashes:Object.fromEntries(Object.values(files).map(n=>[n,crypto.createHash('sha256').update(fs.readFileSync(path.join(reference,n))).digest('hex')])),scope:'Executes original GameStartCtrl and GameEndCtrl methods after mechanical AS3 syntax adaptation. UI/transition callbacks and fighter intro/win states are declared scenario inputs. SHOW_CONTINUE=false and single mode. Not Flash VM, root listener ordering, raster, wall-clock dispatch, or team HP healing.',trace:rows};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),rows.length+'\n'+rows.map(r=>r.join(' ')).join('\n')+'\n');console.log('Original start/end states',rows.length);
