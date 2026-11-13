// SPDX-License-Identifier: GPL-3.0-or-later
import fs from'node:fs';import path from'node:path';import crypto from'node:crypto';
import{methodAdapter}from'./source_method_adapter.mjs';
const [reference,deps,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE DEPS OUTPUT.json');
const base='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={ui:path.join(reference,base+'ui/musou/MusouUI.as'),start:path.join(reference,base+'ctrler/game_ctrls/GameStartCtrl.as'),timeout:path.join(deps,'LIB_KyoLib/src/net/play5d/kyo/utils/KyoTimeout.as')};
const source=Object.fromEntries(Object.entries(files).map(([k,p])=>[k,fs.readFileSync(p,'utf8')]));
const Event={COMPLETE:'complete',ENTER_FRAME:'frame'},GameConfig={FPS_GAME:30,SHOW_UI_STATUS:0};
const GameCtrl={I:null},EffectCtrl={I:null},SoundCtrl={I:null},GameUI={I:null},KyoTimeout={};
const method=methodAdapter(source,{Event,GameConfig,GameCtrl,EffectCtrl,SoundCtrl,GameUI,KyoTimeout,Point:function(x,y){this.x=x;this.y=y;},FighterActionState:{BISHA_ING:12,BISHA_SUPER_ING:13,KAI_CHANG:60},trace(){},Math});
const methods=(key,names)=>Object.fromEntries(names.map(n=>[n,method(key,n)]));
const uiMethods=methods('ui',['showStart','showBossIn','showBossKO','showWin','showLose','renderStartAndKO','playStartKO']);
const startMethods=methods('start',['startMusou','render','renderStartMusou','skip']);
const timeoutMethods=methods('timeout',['setFrameTimeout','setTimeout','setListener','onEnterFrame']);
const stops=new Set([0,69,175,247,299,348]);
const labels={mission_start:9,boss_in:79,boss_ko:184,mission_complete:258,mission_fail:315};
const hashQueue=q=>{let h=2166136261;const word=v=>{for(let b=0;b<4;++b)h=Math.imul(h^((v>>>(b*8))&255),16777619)>>>0;};word(q.length);for(const entry of q){word(+!!entry);if(entry){word(entry.func.name==='playKO2'?0:entry.func.name==='win'?1:2);word(entry.frame);}}return h;};
const rows=[];
for(const hz of[30,60])for(let scenario=0;scenario<4;++scenario){
 GameConfig.FPS_GAME=hz;let listening=false,flat=true,completions=0,signals=0,victories=0,startActive=true,bossDone=false,ending=false,returnRequested=false,timeoutRoots=0,uiPhase=0,stripFrame=0;
 Object.assign(KyoTimeout,timeoutMethods,{_functions:[],_root:{stage:{frameRate:hz},removeEventListener(){listening=false;},addEventListener(){listening=true;}}});
 let listeners=[];
 const mc={x:0,y:0,scaleX:1,scaleY:1,frame:0,get currentFrameLabel(){return stops.has(this.frame)?'stop':null;},
  addEventListener(e,fn){if(!listeners.includes(fn))listeners.push(fn);},removeEventListener(e,fn){listeners=listeners.filter(x=>x!==fn);},
  gotoAndStop(n){this.frame=typeof n==='number'?n-1:labels[n];stripFrame=0;},
  nextFrame(){++this.frame;if(this.frame===90||this.frame<90||this.frame>=164)stripFrame=0;if(this.frame===39)signals|=1;if(this.frame===80)signals|=2;if(stops.has(this.frame)){++completions;for(const fn of listeners.slice())fn({});}}};
 const actors=[0,1,2].map(id=>({x:400,y:500,actionState:0,getDisplay(){return id;},win:function win(){victories|=1<<id;}}));
 let leader=0;
 GameCtrl.I={gameRunData:{p1FighterGroup:{get currentFighter(){return actors[leader];}}},gameState:{cameraFocusOne(){}},pause(){flat=false;},resume(){flat=true;}};
 EffectCtrl.I={doEffectById(){},shine(){},slowDown(){},shake(){},BGEffect(){},freezeEnabled:true};
 SoundCtrl.I={playSwcSound(name){if(name==='ko'||name==='bs')signals|=4;}};
 const ui={...uiMethods,_startAndKoMc:mc,_startAndKoPos:{x:0,y:0},snd_over_hit:'hit',snd_ko:'ko',snd_ko_bs:'bs'};
 GameUI.I={getUI:()=>ui};
 const start={...startMethods,_state:{gameUI:{getUI:()=>ui}},_isStart1v1:false,_isStartNextRound:false,_isStartMusou:false,_musouFinish:false};start.startMusou();
 for(let tick=0;tick<hz*39;++tick){
  const at=second=>tick===hz*second;
  let op=0;
  if(at(4)||at(22))op=1;
  if(at(9)||at(15))op=2;
  if(at(27))op=scenario===2?4:3;
  if(scenario===3&&tick===9*hz+1)op=2;
  const paused=scenario===1&&((tick>=9*hz+4&&tick<11*hz)||(tick>=27*hz+4&&tick<29*hz)||(tick>=33*hz&&tick<37*hz));
  signals=0;const wasListening=listening;stripFrame=mc.frame>=90&&mc.frame<164?(stripFrame+1)%200:0;
  if(op===1){bossDone=false;ui.showBossIn(()=>{bossDone=true;signals|=8;});}
  if(op===2){actors[leader].actionState=12;ui.showBossKO(actors[1],()=>{});}
  if(op===3){leader=2;ending=true;ui.showWin(()=>{returnRequested=true;});}
  if(op===4){ending=true;ui.showLose(()=>{returnRequested=true;});}
  if(tick===3)start.skip();
  let animate=false;
  if(!paused){animate=hz===30||++uiPhase>=2;if(animate){uiPhase=0;ui.renderStartAndKO();}
   if(flat&&startActive&&start.render())startActive=false;
  }
  if(wasListening){++timeoutRoots;KyoTimeout.onEnterFrame(null);}
  const frame=mc.frame,kind=frame<79?1:frame<184?2:frame<258?3:frame<315?4:5;
  rows.push([hz,scenario,tick,op,+paused,+animate,frame,kind,+startActive,+bossDone,+ending,+returnRequested,+flat,listeners.filter(x=>x.name==='koBack').length,completions,victories,signals,hashQueue(KyoTimeout._functions),timeoutRoots,stripFrame]);
 }
}
const report={rows:rows.length,sourceHashes:Object.fromEntries(Object.values(files).map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex')])),scope:'Executes pinned MusouUI showStart/showBossIn/showBossKO/showWin/showLose/playStartKO/renderStartAndKO, GameStartCtrl Musou/skip and KyoTimeout methods after mechanical AS3 syntax adaptation. Desktop SHOW_UI_STATUS=0. XFL-labelled stop/COMPLETE and sound frames use a declared MovieClip adapter. Root callback order, scriptless warning-strip construction/independent frame advancement, and pause windows are declared fixture inputs. Callback IDs normalize captured functions; victory actor identity is checked through callback results. Not Flash VM/broadcast/raster or full Musou combat/session proof.',trace:rows};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),rows.length+'\n'+rows.map(r=>r.join(' ')).join('\n')+'\n');console.log('Original Musou session states',rows.length);
