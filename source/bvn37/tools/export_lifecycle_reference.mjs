// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {methodAdapter} from './source_method_adapter.mjs';
const [reference,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE OUTPUT.json');
const base='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={ui:base+'ui/musou/MusouUI.as',ctrl:base+'ctrler/musou_ctrls/MusouCtrl.as',effect:base+'ctrler/EffectCtrl.as',timeout:'libraries/LIB_KyoLib/src/net/play5d/kyo/utils/KyoTimeout.as'};
const source=Object.fromEntries(Object.entries(files).map(([k,p])=>[k,fs.readFileSync(path.join(reference,p),'utf8')]));
const Event={COMPLETE:'complete',ENTER_FRAME:'frame'},GameConfig={FPS_GAME:30,SHOW_UI_STATUS:0};
const GameCtrl={I:null},EffectCtrl={I:null},SoundCtrl={I:null},GameUI={I:null},KyoTimeout={},MusouLogic={I:null};
const GameEvent={MUSOU_MISSION_FINISH:1,dispatchEvent(){}};
const method=methodAdapter(source,{Event,GameConfig,GameCtrl,EffectCtrl,SoundCtrl,GameUI,KyoTimeout,MusouLogic,GameEvent,AssetManager:{I:{getSound:()=>null}},Point:function(x,y){this.x=x;this.y=y;},FighterActionState:{BISHA_ING:12,BISHA_SUPER_ING:13},TraceLang(){},trace(){},Math});
const methods=(key,names)=>Object.fromEntries(names.map(n=>[n,method(key,n)]));
const uiMethods=methods('ui',['showStart','showBossIn','showBossKO','showWin','showLose','renderStartAndKO','playStartKO']);
const ctrlMethods=methods('ctrl',['missionComplete','onSelfDead','onTimeOver']);
const timeoutMethods=methods('timeout',['setFrameTimeout','setTimeout','setListener','onEnterFrame']);
const stops=new Set([0,69,175,247,299,348]),labels={mission_start:9,boss_in:79,boss_ko:184,mission_complete:258,mission_fail:315};
const word=(h,v)=>{for(let i=0;i<4;++i)h=Math.imul(h^((v>>>(8*i))&255),16777619)>>>0;return h;};
const queueHash=q=>{let h=word(2166136261,q.length);for(const e of q){h=word(h,+!!e);if(e){h=word(h,e.func.name==='playKO2'?0:e.func.name==='win'?1:2);h=word(h,e.frame);}}return h;};
const trace=[];
for(const hz of [30,60])for(let scenario=0;scenario<16;++scenario){
 GameConfig.FPS_GAME=hz;let listening=false,listeners=[],completions=0,signals=0,victories=0,leader=0,flat=true,startActive=true,startDone=false,entranceDone=false,ending=false,returnRequested=false,passRequested=false,returns=0,passes=0,returnHash=2166136261,timeoutRoots=0,uiPhase=0,stripFrame=0,callKind=0,result=1,revision=0;
 const listenerKind=new WeakMap();
 Object.assign(KyoTimeout,timeoutMethods,{_functions:[],_root:{stage:{frameRate:hz},removeEventListener(){listening=false;},addEventListener(){listening=true;}}});
 const mc={x:0,y:0,scaleX:1,scaleY:1,frame:0,get currentFrameLabel(){return stops.has(this.frame)?'stop':null;},
  addEventListener(e,fn){if(!listeners.includes(fn)){listeners.push(fn);listenerKind.set(fn,fn.name==='koBack'?2:callKind);}},removeEventListener(e,fn){listeners=listeners.filter(x=>x!==fn);},
  gotoAndStop(n){this.frame=typeof n==='number'?n-1:labels[n];stripFrame=0;},
  nextFrame(){++this.frame;if(this.frame===90||this.frame<90||this.frame>=164)stripFrame=0;if(this.frame===39)signals|=1;if(this.frame===80)signals|=2;if(stops.has(this.frame)){++completions;for(const fn of listeners.slice())if(listeners.includes(fn))fn({});}}};
 const actors=[0,1,2].map(id=>({x:400,y:500,actionState:12,getDisplay(){return id;},win:function win(){victories|=1<<id;}}));
 GameCtrl.I={actionEnable:true,gameRunData:{p1FighterGroup:{get currentFighter(){return actors[leader];}}},gameState:{cameraFocusOne(){}},pause(){flat=false;},resume(){flat=true;}};
 EffectCtrl.I={doEffectById(){},shine(){},slowDown(){},shake(){},BGEffect(){},freezeEnabled:true};
 SoundCtrl.I={BGM(){},playSwcSound(name){if(name==='ko'||name==='bs')signals|=4;}};
 const ui={...uiMethods,_startAndKoMc:mc,_startAndKoPos:{x:0,y:0},snd_over_hit:'hit',snd_ko:'ko',snd_ko_bs:'bs'};
 GameUI.I={showMusouWin(callback){callKind=3;ui.showWin(callback);},showMusouLose(callback){callKind=4;ui.showLose(callback);}};
 MusouLogic.I={passMission(){++passes;passRequested=true;}};
 const ctrl={...ctrlMethods,_mission:0,_missionComplete:false,_gameFinish:false,_runningWave:{},backToWorldMap(){returnRequested=true;++returns;returnHash=word(returnHash,passes>lastPasses?1:0);lastPasses=passes;}};let lastPasses=0;
 callKind=0;ui.showStart(()=>{startDone=true;});
 const schedules=[[[3,1],[3+1/hz,2]],[[3,2],[3+1/hz,1]],[[3,1],[4.5,2]],[[3,2],[5,1]],[[3,1],[3.5,1],[4,1]],[[3,2],[3.2,2],[3.4,1]],[[3,3],[3.5,1]],[[3,1],[3.5,3]],[[2.9,4],[3,1],[3.1,2]],[[3,1],[3.5,4]],[[3,2],[4,1],[7.7,2]],[[3,1],[5,2],[7,1]],[[3,3],[3.2,3],[3.5,2],[4,1]],[[2.5,5],[3,1],[3.5,2]],[[3,2],[3,1]],[[1/hz,1],[2/hz,2],[3,1]]][scenario];
 for(let tick=0;tick<hz*16;++tick){
  const ops=schedules.filter(([at])=>Math.round(at*hz)===tick).map(x=>x[1]);let encoded=0;for(let i=0;i<ops.length;++i)encoded|=ops[i]<<(3*i);
  const paused=scenario%3===0&&tick>=4*hz&&tick<5*hz;
  signals=0;const wasListening=listening;stripFrame=mc.frame>=90&&mc.frame<164?(stripFrame+1)%200:0;leader=(Math.floor(tick/hz)+scenario)%3;
  for(const op of ops){
   if(op===1){const accepted=!ctrl._missionComplete;ctrl.missionComplete();if(accepted){result=2;++revision;ending=true;}}
   if(op===2){ctrl.onSelfDead(actors[leader]);result=4;++revision;ending=true;}
   if(op===3){ctrl.onTimeOver();result=3;++revision;ending=true;}
   if(op===4){flat=false;ui.showBossKO(actors[leader],()=>{});}
   if(op===5){callKind=1;entranceDone=false;ui.showBossIn(()=>{entranceDone=true;signals|=8;});}
  }
  let animate=false;if(!paused){animate=hz===30||++uiPhase>=2;if(animate){uiPhase=0;ui.renderStartAndKO();}if(flat&&startActive&&startDone)startActive=false;}
  if(wasListening){++timeoutRoots;KyoTimeout.onEnterFrame(null);}
  const frame=mc.frame,kind=frame<79?1:frame<184?2:frame<258?3:frame<315?4:5;
  trace.push([hz,scenario,tick,encoded,+paused,+animate,leader,frame,kind,+startActive,+entranceDone,+ending,+returnRequested,+passRequested,returns,listeners.filter(fn=>[3,4].includes(listenerKind.get(fn))).length,listeners.filter(fn=>listenerKind.get(fn)===2).length,completions,victories,signals,queueHash(KyoTimeout._functions),timeoutRoots,stripFrame,result,revision,+ctrl._missionComplete,+ctrl._gameFinish,passes,returnHash,+flat]);
 }
}
const fade=[];const render=method('effect','renderRemoveEnemy');
for(let scenario=0;scenario<128;++scenario){
 const initial=(scenario%17)/16;let alpha=initial,callbacks=0;const display={};Object.defineProperty(display,'alpha',{get(){return alpha;},set(v){alpha=Math.min(1,Math.max(0,v));}});
 const controller={_removeEnemieMap:{one:{fighter:{getDisplay:()=>display},callback(){++callbacks;}}}};
 for(let tick=0;tick<64;++tick){const write=(tick*(scenario%7+1)+scenario)%11===0;const value=((tick*13+scenario*7)%151-25)/100;if(write)display.alpha=value;const animate=tick%5!==scenario%5;if(animate)render.call(controller);fade.push([scenario,tick,initial,+write,value,+animate,alpha,+!!controller._removeEnemieMap.one,callbacks]);}
}
const report={rows:trace.length,columns:30,fadeRows:fade.length,sourceHashes:Object.fromEntries(Object.values(files).map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(path.join(reference,p))).digest('hex')])),scope:'Mechanically executes original MusouCtrl missionComplete/onSelfDead/onTimeOver, MusouUI methods, KyoTimeout and EffectCtrl.renderRemoveEnemy. Declares SHOW_UI_STATUS=0, labelled MovieClip stop/COMPLETE frames, listener snapshot order, root registration timing, no-op audio/camera, selected event/alpha-write schedules and clamped display alpha. Native game scenes separately exercise real DEAD events and fades. Not full AVM2/event broadcaster/raster or arbitrary scripts.',trace,fade};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),trace.length+'\n'+trace.map(r=>r.join(' ')).join('\n')+'\n');fs.writeFileSync(output.replace(/\.json$/,'.ALPHA.IN'),fade.length+'\n'+fade.map(r=>r.join(' ')).join('\n')+'\n');console.log('Original lifecycle states',trace.length,'shared alpha states',fade.length);
