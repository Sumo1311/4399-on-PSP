// SPDX-License-Identifier: GPL-3.0-or-later
import fs from'node:fs';import path from'node:path';import crypto from'node:crypto';
import {methodAdapter} from'./source_method_adapter.mjs';
const [reference,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE OUTPUT.json');
const base='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/ui/';
const files={hp:base+'HpBarLerp.as',fill:base+'energy/EnergyBarFill.as',label:base+'energy/EnergyBarLabel.as',energy:base+'fight/EnergyBar.as',qi:base+'fight/QiBar.as'};
const source=Object.fromEntries(Object.entries(files).map(([k,p])=>[k,fs.readFileSync(path.join(reference,p),'utf8')]));
const GameConfig={FPS_GAME:30},FighterActionState={HURT_ING:1,HURT_FLYING:2,HURT_DOWN:3};
const method=methodAdapter(source,{GameConfig,FighterActionState,GameUI:{BITMAP_UI:true},Math});
const methods=(key,names)=>Object.fromEntries(names.map(n=>[n,method(key,n)]));
const hp=methods('hp',['render']),fill=methods('fill',['render','normal','flash','overLoad','renderFlash']);
const label=methods('label',['render','normal','flash','overLoad','renderFlash']);
const energy=methods('energy',['render']),qi=methods('qi',['render','fadIn','fadOut']);
const rows=[];
for(const hz of[30,60])for(let scenario=0;scenario<16;++scenario){
 GameConfig.FPS_GAME=hz;
 const h={...hp,_bar:{scaleX:1},_redBar:{scaleX:1},_hprate:1,_redBarMoving:false,_redBarMoveDelay:0,_justHurtFly:false};
 const mc=()=>({scaleX:1,frame:1,gotoAndStop(n){this.frame=n;}});
 const f={...fill,_mc:mc(),rate:1,_isOverLoad:false,_isFlash:false,_renderFlashInt:0,_renderFlashFrame:2};
 const l={...label,_mc:{mc:mc()},_isOverLoad:false,_isFlash:false,_renderFlashInt:0,_renderFlashFrame:0};
 const e={...energy,_fighter:{energy:100,energyMax:100,energyOverLoad:false},_bar:f,_txt:l};
 const q={...qi,_fighter:{qi:0,fzqi:1,fzqiMax:1},_bar:{v:0,getProcess(){return this.v;},setProcess(v){this.v=v;}},_fzBar:{v:0,getProcess(){return this.v;},setProcess(v){this.v=v;}},_fzReadyMc:{visible:true,mc:{frame:1,playing:true,play(){this.playing=true;},gotoAndStop(frame){this.frame=frame;this.playing=false;}}},_ui:{visible:false},_isFadIn:false};
 let maxFrame=0;
 for(let tick=0;tick<240;++tick){
  const rate=[1,.2,.67,.65,.99,0,1.1][(Math.floor(tick/19)+scenario)%7];
  const state=[0,1,1,2,2,3,0,4][(Math.floor(tick/9)+scenario)%8];
  const erate=[1,.29,.1,.7,.5,0][(Math.floor(tick/13)+scenario)%6],overload=((tick+scenario*5)%53)<9;
  const qrate=[0,10,99,101,200,299,300,450,17][(Math.floor(tick/11)+scenario)%9];
  const assist=[1,.99,0,.4,1.1][(Math.floor(tick/7)+scenario)%5];
  const visibility=tick%41===0?1:tick%41===33?2:0;
  if(visibility===1)q.fadIn();if(visibility===2)q.fadOut();
  // Declared nested MovieClip ENTER_FRAME adapter, separate from methods.
  if(q._fzReadyMc.mc.playing)q._fzReadyMc.mc.frame=q._fzReadyMc.mc.frame%20+1;
  maxFrame=(maxFrame+1)%14;
  h.render({hp:rate*1000,hpMax:1000,actionState:state});
  e._fighter.energy=erate*100;e._fighter.energyOverLoad=overload;e.render();
  q._fighter.qi=qrate;q._fighter.fzqi=assist;q.render();
  if(f._mc.frame!==l._mc.mc.frame)throw Error('Fill/label diverged');
  rows.push([hz,scenario,tick,rate,state,erate,+overload,qrate,assist,visibility,h._bar.scaleX,h._redBar.scaleX,h._redBarMoveDelay,+h._redBarMoving,+h._justHurtFly,f._mc.scaleX,f._mc.frame,f._renderFlashInt,+f._isFlash,+f._isOverLoad,q._bar.v,q._fzBar.v,+q._ui.visible,+q._fzReadyMc.visible,q._fzReadyMc.mc.frame-1,+q._fzReadyMc.mc.playing,maxFrame]);
 }
}
const report={rows:rows.length,sourceHashes:Object.fromEntries(Object.values(files).map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(path.join(reference,p))).digest('hex')])),scope:'Pinned original HpBarLerp, EnergyBar/Fill/Label and QiBar methods after mechanical syntax adaptation. Actor states/gauges are inputs; nested 20/14-frame indicator playback is a declared adapter. Desktop bitmap visibility only. Not native action-state mapping or Flash VM/raster/broadcast proof.',trace:rows};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),rows.length+'\n'+rows.map(r=>r.join(' ')).join('\n')+'\n');console.log('Original HUD states',rows.length);
