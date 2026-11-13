// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {methodAdapter} from './source_method_adapter.mjs';
const [reference,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE OUTPUT.json');
const base='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const files={stage:base+'stage/GameStage.as',sprite:base+'interfaces/BaseGameSprite.as',effect:base+'ctrler/EffectCtrl.as',view:base+'views/effects/EffectView.as',black:base+'views/effects/BlackBackView.as',handler:base+'ctrler/effect/EffectBlackBackHandler.as',shadow:base+'views/effects/ShadowEffectView.as'};
const sources=Object.fromEntries(Object.entries(files).map(([k,p])=>[k,fs.readFileSync(path.join(reference,p),'utf8')]));
const key=(kind,id=0)=>kind*4294967296+id;
class DisplayObject{constructor(value){this.key=value;this.parent=null;this.x=this.y=0;this.scaleX=this.scaleY=1;}getBounds(){return {x:0,y:0};}}
class MovieClip extends DisplayObject{}
class Container extends DisplayObject{
 constructor(){super(-1);this.children=[];}
 get numChildren(){return this.children.length;}
 addChild(child){return this.addChildAt(child,this.children.length);}
 addChildAt(child,index){if(index<0||index>this.children.length)throw RangeError('index');if(child.parent)child.parent.removeChild(child);index=Math.min(index,this.children.length);this.children.splice(index,0,child);child.parent=this;return child;}
 removeChild(child){const index=this.children.indexOf(child);if(index<0)throw Error('not child');this.children.splice(index,1);child.parent=null;return child;}
 getChildIndex(child){const index=this.children.indexOf(child);if(index<0)throw Error('not child');return index;}
 setChildIndex(child,index){if(index<0||index>=this.children.length)throw RangeError('index');this.removeChild(child);this.children.splice(index,0,child);child.parent=this;}
 swapChildren(a,b){const ai=this.getChildIndex(a),bi=this.getChildIndex(b);[this.children[ai],this.children[bi]]=[b,a];}
}
class BaseGameSprite{
 constructor(id){this.display=new DisplayObject(key(0,id));this.active=false;this.destroyed=false;}
 getDisplay(){return this.display;}setActive(v){this.active=v;}destroy(){this.destroyed=true;}setVolume(){}setSpeedRate(){}
}
const map={setVisible(){}};let stage,selectedShadow;
const globals={BaseGameSprite,FighterMain:BaseGameSprite,DisplayObject,MovieClip,GameData:{I:{config:{soundVolume:1}}},GameConfig:{SPEED_PLUS:1},EffectModel:{I:{getEffect:id=>({id})}},DisplayFrameBitmapCache:{I:{drawShadow:()=>({bitmapData:{}}),borrowBitmap:()=>selectedShadow,isShadowCached:()=>false}},GameCtrl:{I:{pause(){},resume(){},setRenderHit(){},gameState:{cameraFocusOne(){},cameraResume(){}}}},SoundCtrl:{I:{playSwcSound(){}}},snd_ghost_jump:0};
const method=methodAdapter(sources,globals);
const stageMethods=Object.fromEntries(['addGameSprite','addGameSpriteAt','removeGameSprite'].map(n=>[n,method('stage',n)]));
const hit=method('sprite','hit'),doEffect=method('effect','doEffectVO'),bgEffect=method('effect','BGEffect'),remove=method('view','removeSelf'),fadeOut=method('black','fadOut'),ghost=method('handler','ghostStep'),shadow=method('shadow','addShadowBp');
let random=0x66c04d17;function next(){random^=random<<13;random^=random>>>17;random^=random<<5;return random>>>0;}
const trace=[];let exits=0,reorders=0;
for(let scenario=0;scenario<64;++scenario){
 const root=new Container(),players=new Container(),sprites=Array.from({length:16},(_,i)=>new BaseGameSprite(i)),shadows=Array.from({length:4},(_,i)=>new DisplayObject(key(4,i)));
 for(let i=0;i<3;++i)root.addChild(new DisplayObject(key(6,i)));
 stage=Object.assign(root,stageMethods,{_gameSprites:[],_playerLayer:players,getMap:()=>map,setVisibleByClass(){}});
 const views=Array.from({length:8},(_,i)=>({display:new DisplayObject(key(1,i)),_onRemoveFuncs:[],addRemoveBack(f){(this._onRemoveFuncs??=[]).push(f);},setTarget(){}}));
 const cueViews=[2,3].map(kind=>Array.from({length:4},(_,i)=>({display:new DisplayObject(key(kind,i)),_onRemoveFuncs:[],setTarget(){}})));
 const fx={_effectLayer:players,_gameStage:stage,_frameEffectCount:{},addEffect(data){return data.kind?cueViews[data.kind-2][data.id]:views[data.id];}};
 const black=Object.assign(new DisplayObject(key(5)),{fadIn(){},removeBishaFace(){}});black.fadOut=fadeOut;
 const handler={_gameStage:stage,_blackBack:black,justRender(){},justRenderAnimate(){}};
 const shadowView={r:0,g:0,b:0,target:sprites[0].display,_alphaStart:.8,container:players,_bps:[],_bpCached:{},buildCacheKey:()=>''};
 for(let tick=0;tick<128;++tick){
  let op=next()%16,who=next()%16,other=next()%16,index=next()%45;
  if(tick<8){op=[0,0,4,3,7,5,7,8][tick];who=[0,1,0,0,0,0,0,0][tick];other=1;}
  const before=players.children.map(x=>x.key).join(',');let fault=0;
  try{
   switch(op){
    case 0:stage.addGameSprite(sprites[who]);break;
    case 1:index=players.numChildren?index%(players.numChildren+1):0;stage.addGameSpriteAt(sprites[who],index);break;
    case 2:stage.removeGameSprite(sprites[who],false);break;
    case 3:hit.call(sprites[who],null,sprites[other]);break;
    case 4:fx._frameEffectCount={};doEffect.call(fx,{id:who%8},0,0);break;
    case 5:bgEffect.call(fx,String(who%8));break;
    case 6:{const view=views[who%8];view._onRemoveFuncs??=[];remove.call(view);break;}
    case 7:ghost.call(handler,sprites[who]);break;
    case 8:black.fadOut();break;
    case 9:selectedShadow=shadows[who%4];shadow.call(shadowView);break;
    case 10:{const display=views[who%8].display;if(!display.parent)throw Error('no parent');display.parent.setChildIndex(display,index);break;}
    case 11:{const a=views[who%8].display,b=sprites[other].display;if(!a.parent)throw Error('no parent');a.parent.swapChildren(a,b);break;}
    case 12:{const a=sprites[who].display;if(!a.parent)throw Error('no parent');a.parent.setChildIndex(a,0);break;}
    case 13:{const a=sprites[who].display,b=sprites[other].display;if(!a.parent)throw Error('no parent');a.parent.swapChildren(a,b);break;}
    case 14:fx._frameEffectCount={};doEffect.call(fx,{id:who%4,kind:2},0,0);break;
    case 15:fx._frameEffectCount={};doEffect.call(fx,{id:who%4,kind:3},0,0);break;
   }
  }catch(e){fault=1;++exits;}
  if(before!==players.children.map(x=>x.key).join(','))++reorders;
  trace.push({input:[scenario,tick,op,who,other,index],fault,logical:stage._gameSprites.map(x=>x.display.key),players:players.children.map(x=>x.key),stage:root.children.map(x=>x.key)});
 }
}
const report={rows:trace.length,declaredDisplayOperationExits:exits,playerOrderChanges:reorders,sourceHashes:Object.fromEntries(Object.values(files).map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(path.join(reference,p))).digest('hex')])),scope:'Mechanically executes pinned GameStage sprite add/add-at/remove, BaseGameSprite.hit, EffectCtrl doEffectVO/BGEffect, EffectView removal, BlackBackView removal, ghost blackback insertion and ShadowEffectView bitmap insertion. Declared stable live sprite/display pools, integer indexed container semantics, empty render/audio/camera callbacks, supplied effect views with frame-count reset and supplied shadow bitmap cache. Includes explicit invalid raw display operations as component exits. Does not execute actual AVM2 display container internals, raster, effect view-cache allocation or parent dispatch/exception propagation. Actual native scene emissions and GPU layering separately tested.',trace};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');
fs.writeFileSync(output.replace(/\.json$/,'.IN'),trace.length+'\n'+trace.map(r=>[...r.input,r.fault,...[r.logical,r.players,r.stage].flatMap(a=>[a.length,...a])].join(' ')).join('\n')+'\n');
console.log('Original display states',trace.length,'declared exits',exits,'player order changes',reorders);
