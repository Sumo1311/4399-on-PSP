// Execute original EffectCtrl.BGEffect and EffectView in declared display stubs.
// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [reference,artFile,output]=process.argv.slice(2);
if(!output)throw Error('usage: export_background_reference.mjs reference art.json BACK.IN');
const root='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/',sources={};
function source(name){const b=fs.readFileSync(path.join(reference,root+name));sources[root+name]=crypto.createHash('sha256').update(b).digest('hex');return b.toString().replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');}
function methods(text,names,env={}){const out={};for(const name of names){
 const m=new RegExp(`(?:public|private) function ${name}\\(([^)]*)\\)(?:\\s*:\\w+)?\\s*\\{`).exec(text);if(!m)throw Error(name);
 let end=m.index+m[0].length,depth=1;const start=end;for(;depth;end++){if(text[end]==='{')depth++;if(text[end]==='}')depth--;}
 const args=m[1].split(',').filter(Boolean).map(a=>a.trim().replace(/:\w+/,''));
 const body=text.slice(start,end-1).replace(/var (\w+):[\w.]+(?:<[^>]+>)?/g,'var $1')
  .replace(/for each\(var (\w+) in _onRemoveFuncs\)/g,'for(var $1 of (_onRemoveFuncs||[]))')
  .replace(/frame is int/g,'Number.isInteger(frame)').replace(/frame is String/g,'typeof frame === "string"')
  .replace(/\)\s*:\s*void\s*\{/g,'){');
 out[name]=Function(...Object.keys(env),`return function(${args}){with(this){${body}}}`)(...Object.values(env));
}return out;}
const art=JSON.parse(fs.readFileSync(artFile)),ids=['kobg','hit_floor'];
const data=ids.map((id,index)=>({id,index,bitmapDataCache:Array.from({length:art.clips.find(c=>c.className===(index?'XG_hitfloor':'kobg_effect_mc')).cache_frames},()=>({bitmapData:{},offsetX:0,offsetY:0})),frameLabelCache:{}}));
const SoundCtrl={I:{playEffectSound(){},playSwcSound(){}}};
const base=methods(source('views/effects/EffectView.as'),['start','renderAnimate','renderDisplay','renderFrameLabel','gotoAndPlay','addRemoveBack','removeSelf'],{SoundCtrl});
const display=base.renderDisplay;base.renderDisplay=function(){this.shown=this._curFrame;display.call(this);};
const EffectModel={I:{getEffect:id=>data.find(d=>d.id===id)}},GameConfig={FPS_ANIMATE:30};
const control=methods(source('ctrler/EffectCtrl.as'),['BGEffect','addEffect','removeEffect'],{EffectModel,GameConfig});
const rows=[];
for(const hz of [30,60])for(let variant=0;variant<4;variant++){
 let visible=true,children=[],pools=new Map();
 const stage={getMap:()=>({setVisible(v){visible=v;}}),addChildAt(d,n){children.splice(n,0,d);}};
 const c={...control,_effects:[],_gameStage:stage,_manager:{getEffectView(d){let pool=pools.get(d)||[];pools.set(d,pool);let v=pool.find(v=>!v.isActive);if(!v){
  const display={scaleX:1,parent:{removeChild(d){const i=children.indexOf(d);if(i>=0)children.splice(i,1);}}};
  v={...base,_data:d,_bitmapDatas:d.bitmapDataCache,_frameLabels:{},_orgX:0,_orgY:0,_rotation:0,_direct:1,_curFrame:0,_isDestroyed:false,_onRemoveFuncs:null,display,isActive:false,autoRemove:true,loopPlay:false,holdFrame:-1};
  display.view=v;pool.push(v);
 }return v;}}};c.removeEffect=c.removeEffect.bind(c);
 for(let tick=0;tick<240;tick++){
  const actions=[];
  if(tick===0)actions.push([0,variant===0?-1:2]);
  if(tick===3)actions.push([1,variant===2?0:0.3]);
  if(tick===12&&variant===3)for(let i=0;i<4;i++)actions.push([0,0.5]);
  if(tick===80)actions.push([0,variant===1?0:-1]);
  if(tick===120)actions.push([0,variant===2?-0.1:1]);
  for(const [id,hold]of actions)c.BGEffect(ids[id],hold);
  const animate=tick%2===0&&!(variant===2&&tick>=20&&tick<36);
  if(animate)for(let i=0;i<c._effects.length;i++)c._effects[i].renderAnimate();
  rows.push([hz,variant,tick,+animate,actions.length,...actions.flat(),+visible,children.length,
   ...children.flatMap(d=>[d.view._data.index,d.view._curFrame,d.view.shown,d.view.holdFrame])].join(' '));
 }
}
fs.writeFileSync(output,rows.length+'\n'+rows.join('\n')+'\n');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
fs.writeFileSync(output+'.json',JSON.stringify({rows:rows.length,sourceHashes:sources,fixtureSha256:sha(fs.readFileSync(output)),artSha256:sha(fs.readFileSync(artFile)),scope:'Original BGEffect/addEffect/removeEffect and EffectView methods with declared AS3 type/loop adapters and bitmap/display/manager stubs. Inputs supply animation cadence and holds; checks child-zero order, hold loops, negative/zero holds, pooled reuse, bypassed frame caps and map-visibility callbacks. Not full Flash VM or original raster.'},null,2)+'\n');
console.log('Exported background reference states',rows.length);
