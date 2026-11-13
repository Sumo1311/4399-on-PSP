// SPDX-License-Identifier: GPL-3.0-or-later
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const [reference,output]=process.argv.slice(2);if(!output)throw Error('REFERENCE OUTPUT.json');
const base='upstream/CORE_KernelLogic/src/net/play5d/game/bvn/';
const paths={ui:base+'ui/fight/FightUI.as',trans:base+'ui/TransUI.as',banner:'flash-source/SRC_XFL/ui/fight/LIBRARY/game_start_mc.xml',transition:'flash-source/SRC_XFL/ui/common/LIBRARY/#链接/MC_transition.xml'};
const sources=Object.fromEntries(Object.entries(paths).map(([k,n])=>[k,fs.readFileSync(path.join(reference,n),'utf8')]));
function timeline(text){
 const labels={},complete=[];
 for(const frame of text.matchAll(/<DOMFrame\b([^>]*)>([\s\S]*?)<\/DOMFrame>/g)){
  const index=Number(/\bindex="(\d+)"/.exec(frame[1])?.[1]);const label=/\bname="([^"]+)"/.exec(frame[1])?.[1];if(label)labels[index]=label;
  const script=/<script[^>]*>([\s\S]*?)<\/script>/.exec(frame[2])?.[1]||'';
  if(script.includes('dispatchEvent(new Event(Event.COMPLETE))')&&!script.includes('function('))complete.push(index);
 }
 return {labels,complete};
}
const banner=timeline(sources.banner),transition=timeline(sources.transition);
for(const [f,n]of Object.entries({9:'start',54:'fight',94:'start_final',139:'go:fight',144:'ko',199:'winner',219:'timeover',266:'drawgame'}))if(banner.labels[f]!==n)throw Error('banner label '+f);
if(JSON.stringify(banner.complete)!=='[87,189,262,308]')throw Error('banner COMPLETE '+banner.complete);
if(transition.labels[9]!=='fadin'||transition.labels[44]!=='fadout'||JSON.stringify(transition.complete)!=='[25,60]')throw Error('transition facts');
const globals={GameConfig:{FPS_UI:30},MainGame:{I:{getFPS:()=>hz}},GameRender:{add(){},remove(){}}};let hz=30;
function method(key,name){
 const text=sources[key].replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
 const match=new RegExp('(?:public|private) function '+name+'\\(([^)]*)\\)(?:\\s*:[\\w.]+)?\\s*\\{').exec(text);if(!match)throw Error(name);
 let begin=match.index+match[0].length,end=begin,depth=1;for(;depth;++end){if(text[end]==='{')++depth;if(text[end]==='}')--depth;}
 const raw=text.slice(begin,end-1),locals=[...raw.matchAll(/var\s+(\w+)\s*:/g)].map(x=>x[1]);
 const args=match[1].split(',').filter(Boolean).map(x=>x.trim().replace(/:[\w.]+/,'')),hidden=new Set([...locals,...args.map(x=>x.split('=')[0].trim())]);
 return Function(...Object.keys(globals),'hidden','return function('+args.join(',')+'){const self=this;const scope=new Proxy({},{has:(t,k)=>!hidden.has(k)&&Reflect.has(self,k),get:(t,k)=>{const v=Reflect.get(self,k,self);return typeof v===\'function\'?v.bind(self):v},set:(t,k,v)=>Reflect.set(self,k,v,self)});with(scope){'+raw.replace(/var\s+(\w+):[\w.]+/g,'var $1')+'}}')(...Object.values(globals),hidden);
}
function clip(facts){return {frame:0,completed:false,get currentFrameLabel(){return facts.labels[this.frame]||null;},gotoAndStop(label){if(typeof label==='number')this.frame=label-1;else{const entry=Object.entries(facts.labels).find(([f,n])=>n===label);if(!entry)throw Error(label);this.frame=Number(entry[0]);}this.completed=facts.complete.includes(this.frame);},nextFrame(){++this.frame;this.completed=facts.complete.includes(this.frame);}};}
const rows=[];const renderBanner=method('ui','renderStartAndKO');
for(const [kind,label]of ['start','start_final','ko','timeover','drawgame','winner'].entries()){
 const mc=clip(banner);mc.gotoAndStop(label);const ui={ui:{startKOmc:mc},renderStartAndKO:renderBanner};
 for(let tick=0;tick<150;++tick){mc.completed=false;ui.renderStartAndKO();rows.push([0,kind,30,tick,mc.frame,+mc.completed,0,0]);}
}
const methods=Object.fromEntries(['render','renderAnimate','startRender','stopRender','fadIn','fadOut'].map(n=>[n,method('trans',n)]));
for(hz of [30,60])for(let kind=0;kind<3;++kind){
 const mc=clip(transition);let calls=0,visible=true;
 const t={...methods,ui:mc,_rendering:false,_fadInBack:null,_fadOutBack:null,_renderAnimateGap:0,_renderAnimateFrame:0};
 if(kind===0)t.fadIn(()=>{++calls;});else t.fadOut(()=>{if(kind===1)++calls;visible=false;});
 for(let tick=0;tick<90;++tick){if(t._rendering)t.render();rows.push([1,kind,hz,tick,mc.frame,calls,+t._rendering,+visible]);}
}
const report={rows:rows.length,fields:['type','kind','hz','tick','frame','complete_or_callbacks','rendering','visible'],facts:{banner,transition},sourceHashes:Object.fromEntries(Object.values(paths).map(n=>[n,crypto.createHash('sha256').update(fs.readFileSync(path.join(reference,n))).digest('hex')])),scope:'Mechanically executes original FightUI.renderStartAndKO and TransUI methods with a declared MovieClip adapter driven by pinned XFL labels and direct COMPLETE frame scripts. Zero-based frames. Winner nested-clip completion, raster, tweens, root registration/removal ordering and wall-clock dispatch are not simulated.',trace:rows};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');fs.writeFileSync(output.replace(/\.json$/,'.IN'),rows.length+'\n'+rows.map(r=>r.join(' ')).join('\n')+'\n');console.log('Original UI/transition states',rows.length);
