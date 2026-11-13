// SPDX-License-Identifier: GPL-3.0-or-later
import fs from'node:fs';import path from'node:path';import crypto from'node:crypto';
const [reference,deps,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE DEPS OUTPUT.json');
const files={render:path.join(reference,'upstream/CORE_KernelLogic/src/net/play5d/game/bvn/ctrler/GameRender.as'),
 timeout:path.join(deps,'LIB_KyoLib/src/net/play5d/kyo/utils/KyoTimeout.as'),
 quick:path.join(reference,'upstream/CORE_KernelLogic/src/net/play5d/game/bvn/ui/QuickTransUI.as'),
 ease:path.join(deps,'LIB_KyoLib/lib/src/com/greensock/easing/Ease.as')};
const sources=Object.fromEntries(Object.entries(files).map(([k,v])=>[k,fs.readFileSync(v,'utf8')]));
const globals={GameConfig:{GAME_SIZE:{x:800,y:600}},Event:{ENTER_FRAME:'frame'},TweenLite:{},Math};
function method(key,name){
 const text=sources[key].replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
 const match=new RegExp('(?:public|private)(?: static)? function '+name+'\\(([^)]*)\\)(?:\\s*:[\\w.]+)?\\s*\\{').exec(text);if(!match)throw Error(key+' '+name);
 let begin=match.index+match[0].length,end=begin,depth=1;for(;depth;++end){if(text[end]==='{')++depth;if(text[end]==='}')--depth;}
 const raw=text.slice(begin,end-1),locals=[...raw.matchAll(/(?:var|,)\s*(\w+)\s*:/g)].map(x=>x[1]);
 const args=match[1].split(',').filter(Boolean).map(x=>x.trim().replace(/:(?:[\w.]+|\*)/,''));
 const hidden=new Set([...locals,...args.map(x=>x.replace(/^\.\.\./,'').split('=')[0].trim())]);
 let body=raw.replace(/for each\s*\(var (\w+):Vector\.<Function> in (\w+)\)/g,'for(var $1 of Object.values($2))').replace(/var\s+(\w+):Vector\.<\w+>/g,'var $1').replace(/var\s+[^;]+/g,v=>v.replace(/:\s*[\w.]+/g,'')).replace(/new Vector\.<\w+>\(\)/g,'[]').replace(/\s+as Function/g,'').replace(/function\s*\(\)\s*:\s*void/g,'function()');
 return Function(...Object.keys(globals),'hidden','return function('+args.join(',')+'){const self=this;const scope=new Proxy({},{has:(t,k)=>!hidden.has(k)&&Reflect.has(self,k),get:(t,k)=>{const v=Reflect.get(self,k,self);return typeof v===\'function\'?v.bind(self):v},set:(t,k,v)=>Reflect.set(self,k,v,self)});with(scope){'+body+'}}')(...Object.values(globals),hidden);
}
const rows=[];
const renderMethods=Object.fromEntries(['addBefore','removeBefore','add','remove','addAfter','removeAfter','render'].map(n=>[n,method('render',n)]));
for(let scenario=0;scenario<256;++scenario){
 const obj={...renderMethods,_beforeFuncs:null,_afterFuncs:null,_funcs:{},isRender:true};let event=0,fired=0;
 const callbacks=Array.from({length:6},(_,id)=>()=>{
  event=(Math.imul(event,17)+id+1)>>>0;
  if(fired&(1<<id))return;fired|=1<<id;
  const mode=(scenario+id)%5,next=(id+1)%6;
  if(mode===0||mode===1){const target=callbacks[mode===0?id:next];obj.removeBefore(target);obj.remove(target);obj.removeAfter(target);}
  if(mode===2)obj.add(callbacks[(id+3)%6]);if(mode===3)obj.addBefore(callbacks[(id+2)%6]);if(mode===4)obj.addAfter(callbacks[next]);
 });
 obj.add(callbacks[0]);obj.add(callbacks[1]);obj.add(callbacks[2]);if(scenario&1)obj.addBefore(callbacks[3]);if(scenario&2)obj.addAfter(callbacks[4]);
 const code=list=>(list||[]).reduce((a,fn)=>a*7+callbacks.indexOf(fn)+1,1);
 for(let tick=0;tick<16;++tick){
  const op=(scenario*17+tick*13)%11,id=(scenario+tick)%6;
  if(op<3)[obj.addBefore,obj.add,obj.addAfter][op].call(obj,callbacks[id]);
  else if(op<6)[obj.removeBefore,obj.remove,obj.removeAfter][op-3].call(obj,callbacks[id]);
  obj.isRender=op!==10;event=fired=0;obj.render(null);
  rows.push([0,scenario,tick,op,event,code(obj._beforeFuncs),code(obj._funcs.anyone),code(obj._afterFuncs)]);
 }
}
const timeoutMethods=Object.fromEntries(['setFrameTimeout','setTimeout','setListener','onEnterFrame'].map(n=>[n,method('timeout',n)]));
function wordHash(values){let hash=2166136261;for(const value of values)for(let b=0;b<4;++b)hash=Math.imul(hash^((value>>>(b*8))&255),16777619)>>>0;return hash;}
for(const hz of [30,60])for(let scenario=0;scenario<16;++scenario){
 let listening=false,adds=0,event=0,fired=0;
 const obj={...timeoutMethods,_functions:[],_root:{stage:{frameRate:hz},removeEventListener(){listening=false;},addEventListener(){listening=true;++adds;}}};
 const callbacks=Array.from({length:4},(_,id)=>(...params)=>{
  event=(Math.imul(event,17)+id+1+params.reduce((a,b)=>a+b,0))>>>0;
  if(fired&(1<<id))return;fired|=1<<id;
  if((scenario&1)&&id<2)obj.setFrameTimeout(callbacks[id+2],id,scenario,id);
 });
 const state=()=>{const v=[obj._functions.length];for(const e of obj._functions){v.push(+!!e);if(e)v.push(callbacks.indexOf(e.func),e.frame,e.param.length,...e.param);}return wordHash(v);};
 for(let tick=0;tick<72;++tick){
  const op=(scenario*7+tick*11)%17;
  if(op<3)obj.setFrameTimeout(callbacks[op],(scenario+tick)%5,scenario,-tick);
  if(op===3)obj.setTimeout(callbacks[3],[0,50,1000][scenario%3],tick);
  event=fired=0;if(listening)obj.onEnterFrame(null);
  rows.push([1,scenario,hz,tick,op,event,state(),+listening,adds,obj._functions.length]);
 }
}
// Preserve original division before multiplication: replacing /1000 with
// *.001 changes ceil at boundaries such as 700 ms / 30 Hz.
for(const hz of [30,60])for(let milliseconds=0;milliseconds<=4000;++milliseconds){
 const obj={...timeoutMethods,_functions:[],_root:{stage:{frameRate:hz},removeEventListener(){},addEventListener(){}}};
 obj.setTimeout(()=>{},milliseconds);rows.push([3,hz,milliseconds,obj._functions[0].frame]);
}
const ease={_func:null,_type:1,_power:1,getRatio:method('ease','getRatio')};
const quickMethods=Object.fromEntries(['fadInAndOut','fadIn','fadOut'].map(n=>[n,method('quick',n)]));
const clocks=[[0,0,16,33,51,99,100,101,117,150,199,200,230],[0,45,190,194,200,291,400]];
for(let scenario=0;scenario<clocks.length;++scenario){
 let now=0,tweens=[],stage=1,complete=0;
 globals.TweenLite.to=(target,duration,vars)=>{tweens.push({target,start:now,duration:duration*1000,from:target.y,to:vars.y,cb:vars.onComplete,active:true});};
 const obj={...quickMethods,_center:300,_up:{y:-300},_down:{y:600}};
 const outMethod=obj.fadOut;obj.fadOut=function(...args){stage=2;return outMethod.apply(obj,args);};
 obj.fadInAndOut(()=>{stage=0;++complete;});
 for(let tick=0;tick<clocks[scenario].length;++tick){
  now=clocks[scenario][tick];
  for(const t of tweens.slice())if(t.active){const p=Math.max(0,Math.min(1,(now-t.start)/t.duration));t.target.y=t.from+(t.to-t.from)*ease.getRatio(p);if(p===1){t.active=false;t.cb?.();}}
  rows.push([2,scenario,tick,now,stage,Math.floor(obj._up.y*1e6+.5),Math.floor(obj._down.y*1e6+.5),complete]);
 }
}
const report={rows:rows.length,renderRows:4096,timeoutRows:10306,quickRows:20,sourceHashes:Object.fromEntries(Object.entries(files).map(([k,n])=>[k,{path:n,sha256:crypto.createHash('sha256').update(fs.readFileSync(n)).digest('hex')}])),dependencyManifest:JSON.parse(fs.readFileSync(path.join(deps,'manifest.json'),'utf8')),scope:'Mechanically executes pinned GameRender methods for one shared owner and before/after vectors with callbacks that add/remove callbacks; KyoTimeout frame conversion, post-decrement/null cleanup and appended callbacks; QuickTransUI with a declared TweenLite ticker adapter and the pinned default Ease.getRatio. No Flash VM, dictionary multi-owner ordering, root broadcast registration, complete TweenLite overwrite/startup/lazy-init, or flash.utils wall-clock dispatch proof.',trace:rows};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),rows.length+'\n'+rows.map(r=>r.join(' ')).join('\n')+'\n');console.log('Original dispatch states',rows.length);
