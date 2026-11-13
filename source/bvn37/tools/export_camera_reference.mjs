// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {methodAdapter} from './source_method_adapter.mjs';
const [reference,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE OUTPUT.json');
const base='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={camera:base+'stage/GameCamera.as',stage:base+'stage/GameStage.as'};
const sources=Object.fromEntries(Object.entries(files).map(([k,p])=>[k,fs.readFileSync(path.join(reference,p),'utf8')]));
class Point{constructor(x=0,y=0){this.x=x;this.y=y;}}
class Rectangle{constructor(x=0,y=0,width=0,height=0){Object.assign(this,{x,y,width,height});}}
const GameConfig={CAMERA_TWEEN_SPD:2.5,GAME_SIZE:new Point(800,600),FPS_ANIMATE:30,FPS_GAME:60,get SPEED_PLUS_DEFAULT(){return this.FPS_ANIMATE/this.FPS_GAME;}};
let Camera;const globals={Point,Rectangle,GameConfig,get GameCamera(){return Camera;},ThrowError(_type,message){throw Error(message);},Error,GetLang:x=>x,Math};
// The constructor binding is installed before selecting methods, preserving
// original constructor and GameStage initialization bodies.
let cameraMethods;
Camera=function(stage,screen,size,border){
 Object.assign(this,cameraMethods,{stageSize:null,focusX:true,focusY:false,offsetX:0,offsetY:0,autoZoom:false,autoZoomMin:1,autoZoomMax:3,_zoom:1,_noTweenRect:null,_stage:null,_stageBounds:null,_rect:null,_focus:null,_point:null,_stageScale:1,_fbR:null,_foffsetX:0,_foffsetY:0,_screenSize:null,_appliedScrollX:NaN,_appliedScrollY:NaN,_appliedScrollW:NaN,_appliedScrollH:NaN,_appliedStageScale:NaN});
 let tween=0;Object.defineProperty(this,'tweenSpd',{get(){return tween;},set(v){tween=v|0;}});
 cameraMethods.GameCamera.call(this,stage,screen,size,border);
};
const method=methodAdapter(sources,{...globals,GameCamera:Camera});
cameraMethods=Object.fromEntries(['GameCamera','setStageBounds','setStageSizeFromDisplay','getZoom','setZoom','focus','move','updateNow','render','setX','setY','applySet','renderTwo','renderAutoZoom','renderX','renderY','renderZoom'].map(n=>[n,method('camera',n)]));
const stageMethods=Object.fromEntries(['initCamera','cameraFocusOne','cameraResume','updateCameraFocus'].map(n=>[n,method('stage',n)]));
let random=0x65ca6e37;function next(){random^=random<<13;random^=random>>>17;random^=random<<5;return random>>>0;}
const widths=[512,800,901.25,1234.123,2047.1937088,2300,4096],trace=[];
let faults=0,forced=0;
for(const hz of [30,60])for(let scenario=0;scenario<64;++scenario){
 GameConfig.FPS_GAME=hz;const width=widths[scenario%widths.length],bottom=600+(scenario%3)*50,playerBottom=bottom-16-(scenario%4)*10;
 let scrollWrites=0,scaleWrites=0,scroll=null,sx=1,sy=1;
 const gameLayer={getBounds(){return new Rectangle(0,0,width,600);}};
 Object.defineProperties(gameLayer,{scrollRect:{get(){return scroll;},set(r){scroll={...r};++scrollWrites;}},scaleX:{get(){return sx;},set(v){sx=v;++scaleWrites;}},scaleY:{get(){return sy;},set(v){sy=v;++scaleWrites;}}});
 const stage={...stageMethods,camera:null,_gameLayer:gameLayer,_map:{bottom,getStageSize(){return new Point(width,600);},getMapBottomDistance(){return bottom-playerBottom;}}};
 stage.initCamera();const camera=stage.camera,actors=Array.from({length:4},()=>new Point());stage._cameraFocus=actors.slice(0,scenario%2?1:2);camera.focus(stage._cameraFocus);
 for(let tick=0;tick<128;++tick){
  for(let i=0;i<4;++i){actors[i].x=(next()%4001-1000)/2;actors[i].y=(next()%2001-1000)/2;}
  let op=next()%15,target=next()%4,count=next()%4,value=(next()%400)/100+.25;
  if(scenario<8&&tick===0){op=1;target=0;}
  if(scenario<8&&tick===1){op=1;target=1;}
  if(scenario<8&&tick===2)op=2;
  let fault=0;
  try{
   switch(op){
    case 1:stage.cameraFocusOne(actors[target]);break;
    case 2:stage.cameraResume();break;
    case 3:stage.updateCameraFocus(actors.slice(0,1+count%3));break;
    case 4:camera.setZoom(value);break;
    case 5:camera.setX(value*100-200);break;
    case 6:camera.setY(value*100-200);break;
    case 7:camera.setStageBounds(new Rectangle((count-1)*10,-1000+target*50,width+(value-2)*100,600+count*40));break;
    case 8:camera.tweenSpd=(value-2)*2;break;
    case 9:camera.focusX=!!(count&1);camera.focusY=!!(count&2);camera.autoZoom=!!(target&1);camera.offsetX=(value-2)*25;camera.offsetY=bottom-playerBottom+(value-2)*10;break;
    case 10:camera.updateNow();++forced;break;
    case 11:camera.tweenSpd=2.5*value;break;
    case 12:camera.move(actors[target].x,actors[target].y);break;
    case 13:camera.focus([]);break;
    case 14:camera.focus(actors.slice(0,count));break;
   }
   camera.render();
  }catch(error){if(!(error instanceof TypeError))throw error;fault=1;++faults;}
  const t=camera._noTweenRect,d=camera._rect,p=camera._point;
  const values=[t.x,t.y,t.width,t.height,d.x,d.y,d.width,d.height,camera._zoom,camera._stageScale,camera.tweenSpd,p?p.x:0,p?p.y:0,scrollWrites,scaleWrites,scroll?scroll.x:0,scroll?scroll.y:0,scroll?scroll.width:0,scroll?scroll.height:0,scroll?camera._appliedStageScale:0,+!!scroll];
  trace.push([hz,scenario,tick,width,bottom,playerBottom,op,target,count,value,...actors.flatMap(p=>[p.x,p.y]),fault,...values]);
 }
}
const report={rows:trace.length,columns:40,intentionalNullOrEmptyFocusExits:faults,successfulForcedUpdates:forced,sourceHashes:Object.fromEntries(Object.values(files).map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(path.join(reference,p))).digest('hex')])),scope:'Mechanically executes original GameCamera constructor/setters/focus/render/updateNow and GameStage initialization/focus-one/resume/update-focus. Declared live Point objects, Number Rectangle properties, copied scrollRect and counted scale setters, fixed 800x600 screen and supplied positive zoom/stage bounds. Includes original move/null-focus and empty-focus TypeError exits; native actual scenes separately require normal valid focuses. Not full Flash VM/display getBounds/raster, arbitrary display lifetimes or parent error propagation.',trace};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),trace.length+'\n'+trace.map(r=>r.join(' ')).join('\n')+'\n');console.log('Original camera states',trace.length,'intentional exits',faults,'forced updates',forced);
